#!/bin/bash
# Stage 13 テスト: 第 1 部 (spawn とシェル)・第 2 部 (ed) の検証
# (docs/stage013-tools.md 4 章・6 章)。
#
#   ビルド再現  ld13.bin / kernel13.bin / sh13 / ed13 の SHA-256 が
#               各 .md 記載値と一致
#   退行        'F' / 'K' の出力が ld12 とバイト一致する ('E' への追加が
#               他形式に影響していないことの証明)
#   互換        ld12 でリンクした Stage 12 のユーザプログラム (hello) が
#               kernel13 でもそのまま動く
#   boot 引数   boot 行の空白区切りが argc / argv として渡る
#   spawn       子の argv・終了コード・ENOENT。親のヒープ・スタック・fd 表が
#               子の実行を挟んで保存される。入れ子 (sh -> mem -> args)
#   シェル      スクリプトを UART から流し，echo・つなぎ替え・`? N`・
#               エラー表示・exit の終了コードを照合する
#   ed          既存ファイルの編集 (n / a / d / = / s///g / p・アドレスのみ・
#               不正コマンド・w・q の警告) と，書いたファイルのホストへの到達
#   同居        シェルから ed を起動し，ed が同じ UART 入力の続きを読み，
#               終了後にシェルがさらに続きを読む
#   移住        ゲスト内で bundle -> pp -> cc -> ldin -> ld を順に走らせ，
#               出来た実行形式をその場で起動する。各段の生成物が
#               ホスト経路の同じ段の生成物とバイト一致する
#   自己再生成  ゲスト内で cc 自身のソースを通し，出来た cc10l.bin が
#               チェーンの成果物とバイト一致する
#   ビルド記述  mk が依存の順に命令を実行し，pp / cc / ld を作り直す。
#               記録済みの成果物とバイト一致し，固定点も成り立つ
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s13

cc=tmp/build/cc.bin
pp=tmp/build/pp.bin
ld12=tmp/build/ld12.bin
ld13=tmp/build/ld13.bin
usr=tests/stage013/user
exp=tests/stage013/expected
fix=tests/stage013/fixtures

RAMSIZE=134217728        # 128 MiB
SFSOFF=67108864          # 0x0400_0000 (ゲスト物理 0x8400_0000)
IMGSIZE=4194304          # 4 MiB

# 指令を含まないソース (// コメントのみ) を pp を通さずビルドする
buildraw() {
    { cat "$usr/$1.c"; printf '\004'; } | sh tools/env.sh qemu "$cc" > "tmp/s13/$1.o" \
        && { printf 'E'; cat "tmp/s13/$1.o"; printf '\0'; } \
            | sh tools/env.sh qemu "$ld13" > "tmp/s13/$1"
}

# pp + 第 13 世代の libc でビルドする。2 個目以降の引数は一緒に並べる
# オブジェクト (libc)
builduser() {
    name=$1
    shift
    sh tools/bundle.sh stage013/libc/include/*.h "$usr/$name.c" \
        | sh tools/env.sh qemu "$pp" > "tmp/s13/$name.i" \
        && sh tools/env.sh qemu "$cc" < "tmp/s13/$name.i" > "tmp/s13/$name.o" \
        && { printf 'E'; cat "tmp/s13/$name.o" "$@"; printf '\0'; } \
            | sh tools/env.sh qemu "$ld13" > "tmp/s13/$name"
}

# root/ をイメージにして RAM ファイルへ埋め，kernel13 を起動する。
# $1 = 出力名, $2 = 標準入力 (省略時 /dev/null)
runos() {
    sh tools/sfs.sh pack tmp/s13/root tmp/s13/fs.img "$IMGSIZE" 128 || return 1
    rm -f tmp/s13/ram
    dd if=/dev/null of=tmp/s13/ram bs=1 seek="$RAMSIZE" 2> /dev/null
    dd if=tmp/s13/fs.img of=tmp/s13/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
        conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE=tmp/s13/ram sh tools/env.sh qemu tmp/build/kernel13.bin \
        < "${2:-/dev/null}" > "tmp/s13/$1.out"
}

# 実行後のイメージを回収して展開する
harvest() {
    dd if=tmp/s13/ram of=tmp/s13/fs2.img bs=64K iflag=skip_bytes,count_bytes \
        skip="$SFSOFF" count="$IMGSIZE" 2> /dev/null
    rm -rf tmp/s13/out2
    sh tools/sfs.sh unpack tmp/s13/fs2.img tmp/s13/out2
}

ensure_build stage013
buildrc=$?

# ---------------------------------------------------------------------------
section "ビルド再現"

ok=0
[ "$buildrc" -eq 0 ] || ok=1
for pair in ld13.bin:stage013/ld13.md kernel13.bin:stage013/kernel.md \
        sh13:stage013/sh.md ed13:stage013/ed.md \
        bundle13:stage013/bundle.md ldin13:stage013/ldin.md \
        eot13:stage013/eot.md mk13:stage013/mk.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: ld13.bin / kernel13.bin / sh13 / ed13 / bundle13 / ldin13 の SHA-256 が各 .md 記載値と一致"

# libc のオブジェクトも記録値と照合する。道具へリンクされるものは実行結果で
# 間接的に守られるが，どの道具にもリンクされない ctype.o は記録した SHA-256 を
# 誰も見ていなかった (記録だけがあって検査が無い状態)
ok=0
[ "$buildrc" -eq 0 ] || ok=1
for f in string ctype stdlib morecore; do
    doc=stage013/libc/src/$f.md
    [ -f "$doc" ] || { ok=1; continue; }
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/l13_src_$f.o"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: libc 純粋部 (string / ctype / stdlib / morecore) の .o が各 .md 記載値と一致"

# pp / cc / ld のコマンド版は素材が凍結された .o なので 1 つの .md にまとめてある
ok=0
[ "$buildrc" -eq 0 ] || ok=1
for n in pp13cmd cc13cmd ld13cmd; do
    got=$(sha256sum "tmp/build/$n"); got=${got%% *}
    grep -q "$got" stage013/cmds.md || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: pp / cc / ld (コマンド版) の SHA-256 が cmds.md 記載値と一致"

# sh13 が ELF 実行形式として妥当であることを readelf で確かめる (verify 層)
sh tools/env.sh run riscv64-unknown-elf-readelf -h -l tmp/build/sh13 > tmp/s13/sh13.readelf 2>&1
grep -q 'EXEC (Executable file)' tmp/s13/sh13.readelf \
    && grep -q 'RISC-V' tmp/s13/sh13.readelf \
    && grep -q '0x86000054' tmp/s13/sh13.readelf \
    && grep -q 'LOAD' tmp/s13/sh13.readelf
report $? "elf: sh13 が ET_EXEC / RISC-V / entry 0x86000054 / PT_LOAD"

# ---------------------------------------------------------------------------
section "退行: 'F' / 'K' の出力が ld12 と一致"

ok=0
for o in pp.o cc10l.o ld.o; do
    { printf 'F'; cat "tmp/build/$o"; printf '\0'; } | sh tools/env.sh qemu "$ld12" > tmp/s13/f_old.bin
    { printf 'F'; cat "tmp/build/$o"; printf '\0'; } | sh tools/env.sh qemu "$ld13" > tmp/s13/f_new.bin
    cmp -s tmp/s13/f_old.bin tmp/s13/f_new.bin || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: 'F' の出力が ld12 とバイト一致 (pp / cc10l / ld)"

{ printf 'K'; cat tmp/build/kernel13.o; printf '\0'; } | sh tools/env.sh qemu "$ld12" > tmp/s13/k_old.bin
{ printf 'K'; cat tmp/build/kernel13.o; printf '\0'; } | sh tools/env.sh qemu "$ld13" > tmp/s13/k_new.bin
cmp -s tmp/s13/k_old.bin tmp/s13/k_new.bin
report $? "regress: 'K' の出力が ld12 とバイト一致 (kernel13.o)"

# ---------------------------------------------------------------------------
section "互換: ld12 の生成物が kernel13 で動く"

sh tools/bundle.sh stage012/libc/include/*.h tests/stage012/user/hello.c \
    | sh tools/env.sh qemu "$pp" > tmp/s13/hello.i \
    && sh tools/env.sh qemu "$cc" < tmp/s13/hello.i > tmp/s13/hello.o \
    && { printf 'E'; cat tmp/s13/hello.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld12" > tmp/s13/hello12
report $? "build: hello (Stage 12 のソースを ld12 でリンク)"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/s13/hello12 tmp/s13/root/hello
printf 'hello\n' > tmp/s13/root/boot
runos hello
rc=$?
[ "$rc" -eq 3 ] && diff -q tmp/s13/hello.out tests/stage012/expected/hello.txt > /dev/null
report $? "run: hello が kernel13 でも argv と終了コード 3 を返す"

# ---------------------------------------------------------------------------
section "boot 行の引数"

buildraw args
report $? "build: args (ELF 実行形式)"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/s13/args tmp/s13/root/args
printf 'args x y\n' > tmp/s13/root/boot
runos args
rc=$?
[ "$rc" -eq 3 ] && diff -q tmp/s13/args.out "$exp/args.txt" > /dev/null
report $? "run: boot 行の空白区切りが argv として渡り，argc = 3 が返る"

# ---------------------------------------------------------------------------
section "spawn とシェル"

buildraw upfilt
report $? "build: upfilt (つなぎ替えの検査用フィルタ)"

builduser mem tmp/build/l13_src_string.o tmp/build/l13_src_stdlib.o \
    tmp/build/l13_posix_sys.o tmp/build/l13_posix_morecore.o \
    tmp/build/l13_posix_stdio.o
report $? "build: mem (libc + spawn。親の保存の検査)"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/sh13 tmp/s13/root/sh
cp tmp/s13/args tmp/s13/root/args
cp tmp/s13/upfilt tmp/s13/root/upfilt
cp tmp/s13/mem tmp/s13/root/mem
cp "$fix/in.txt" tmp/s13/root/in.txt
cp "$fix/script2.txt" tmp/s13/root/script2.txt
printf 'sh\n' > tmp/s13/root/boot
runos sh "$fix/script.txt"
rc=$?
[ "$rc" -eq 5 ] && diff -q tmp/s13/sh.out "$exp/sh.txt" > /dev/null
report $? "run: シェルがスクリプトを実行し，exit 5 が終了コードになる"

harvest
printf 'HELLO, STONE!\n' | diff -q - tmp/s13/out2/up.txt > /dev/null
report $? "run: 'upfilt < in.txt > up.txt' の結果がホストへ届く"

diff -q "$fix/in.txt" tmp/s13/out2/in.txt > /dev/null
report $? "run: 入力側のファイルが変わっていない"

# ---------------------------------------------------------------------------
section "ed: 行エディタ (第 2 部)"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/ed13 tmp/s13/root/ed
cp "$fix/notes.txt" tmp/s13/root/notes.txt
printf 'ed notes.txt\n' > tmp/s13/root/boot
runos ed "$fix/ed-script.txt"
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s13/ed.out "$exp/ed.txt" > /dev/null
report $? "run: 編集の一連 (n / a / d / = / s///g / p / 不正 / w / q の警告)"

harvest
printf 'ONE two ONE\ninserted\nline three\n' | diff -q - tmp/s13/out2/out.txt > /dev/null
report $? "run: w で書いたファイルがホストへ届く"

diff -q "$fix/notes.txt" tmp/s13/out2/notes.txt > /dev/null
report $? "run: 開いただけのファイルは変わっていない"

# ---------------------------------------------------------------------------
section "ed とシェルの同居"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/sh13 tmp/s13/root/sh
cp tmp/build/ed13 tmp/s13/root/ed
printf 'sh\n' > tmp/s13/root/boot
runos shed "$fix/shed-script.txt"
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s13/shed.out "$exp/shed.txt" > /dev/null
report $? "run: シェルから起動した ed が入力の続きを読み，戻るとシェルが続きを読む"

harvest
printf 'alpha\nbeta\n' | diff -q - tmp/s13/out2/made.txt > /dev/null
report $? "run: ed が作ったファイルがホストへ届く"

# ---------------------------------------------------------------------------
section "移住: ゲスト内でのビルド (第 3 部)"

rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/sh13 tmp/s13/root/sh
cp tmp/build/bundle13 tmp/s13/root/bundle
cp tmp/build/ldin13 tmp/s13/root/ldin
cp tmp/build/pp13cmd tmp/s13/root/pp
cp tmp/build/cc13cmd tmp/s13/root/cc
cp tmp/build/ld13cmd tmp/s13/root/ld
cp "$fix/hi.c" tmp/s13/root/hi.c
printf 'sh\n' > tmp/s13/root/boot
runos gb "$fix/guestbuild.txt"
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s13/gb.out "$exp/gb.txt" > /dev/null
report $? "run: bundle -> pp -> cc -> ldin -> ld を通し，出来た像がその場で動く"

harvest

# 同じソースをホスト経路に通し，各段の生成物を突き合わせる。
# 移したのが同じ処理系であることの機械的な証明である (8 章)
sh tools/bundle.sh "$fix/hi.c" > tmp/s13/host.b
sh tools/env.sh qemu "$pp" < tmp/s13/host.b > tmp/s13/host.i
sh tools/env.sh qemu "$cc" < tmp/s13/host.i > tmp/s13/host.o
{ printf 'E'; cat tmp/s13/host.o; printf '\0'; } > tmp/s13/host.ld
sh tools/env.sh qemu "$ld13" < tmp/s13/host.ld > tmp/s13/host.bin

ok=0
for pair in host.b:hi.b host.i:hi.i host.o:hi.o host.ld:hi.ld host.bin:hi; do
    cmp -s "tmp/s13/${pair%%:*}" "tmp/s13/out2/${pair##*:}" || ok=1
done
[ "$ok" -eq 0 ]
report $? "same: 各段の生成物 (束ね / .i / .o / ld 入力 / 実行形式) がホスト経路とバイト一致"

# 出来た実行形式が ELF として妥当であることを確かめる (verify 層)
cp tmp/s13/out2/hi tmp/s13/guest-hi
sh tools/env.sh run riscv64-unknown-elf-readelf -h tmp/s13/guest-hi > tmp/s13/hi.readelf 2>&1
grep -q 'EXEC (Executable file)' tmp/s13/hi.readelf \
    && grep -q 'RISC-V' tmp/s13/hi.readelf \
    && grep -q '0x86000054' tmp/s13/hi.readelf
report $? "elf: ゲストが作った像が ET_EXEC / RISC-V / entry 0x86000054"

# ---------------------------------------------------------------------------
section "自己再生成: 処理系が OS の上で自分自身を作り直す (第 3 部)"

# cc 自身のソースは指令を含まないので束ねを通さない。EOT を足すのが eot。
# 出来上がりはチェーンの成果物そのもの (docs/stage013-tools.md 7.4)
rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/sh13 tmp/s13/root/sh
cp tmp/build/eot13 tmp/s13/root/eot
cp tmp/build/ldin13 tmp/s13/root/ldin
cp tmp/build/cc13cmd tmp/s13/root/cc
cp tmp/build/ld13cmd tmp/s13/root/ld
cp stage010/cc12.sc tmp/s13/root/cc12.sc
printf 'sh\n' > tmp/s13/root/boot
runos self "$fix/selfbuild.txt"
report $? "run: eot -> cc -> ldin -> ld が cc12.sc を通る"

harvest
cmp -s tmp/s13/out2/cc10l.bin tmp/build/cc10l.bin
report $? "same: ゲストが作った cc10l.bin がチェーンの成果物とバイト一致"

cmp -s tmp/s13/out2/cc12.o tmp/build/cc10l.o
report $? "same: 途中の cc12.o もホスト経路の cc10l.o とバイト一致"

# ---------------------------------------------------------------------------
section "ビルド記述: mk がゲスト内で処理系を作り直す (第 4 部)"

# 持ち込むのは cc8.o だけ (鎖の境目。docs/stage013-tools.md 9.1 / 9.5)。
# そこから上の pp / cc / ld をゲスト内で作り直す。
# 中間物が増えるのでイメージを広げる (以降の検査はこの節で終わり)
IMGSIZE=16777216
rm -rf tmp/s13/root
mkdir -p tmp/s13/root
cp tmp/build/sh13 tmp/s13/root/sh
cp tmp/build/mk13 tmp/s13/root/mk
cp tmp/build/eot13 tmp/s13/root/eot
cp tmp/build/ldin13 tmp/s13/root/ldin
cp tmp/build/cc13cmd tmp/s13/root/cc
cp tmp/build/ld13cmd tmp/s13/root/ld
cp tmp/build/cc8.o tmp/s13/root/cc8.o
cp stage009/pp.sc stage010/cc12.sc stage013/ld13.sc tmp/s13/root/
cp "$fix/mkfile" tmp/s13/root/mkfile
printf 'sh\n' > tmp/s13/root/boot
printf 'mk -f mkfile all\nexit 0\n' > tmp/s13/mkrun.txt
runos mk tmp/s13/mkrun.txt
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s13/mk.out "$exp/mk.txt" > /dev/null
report $? "run: mk が依存の順に命令を実行し，実行行を出す"

harvest
ok=0
for pair in pp.bin:pp.bin cc10l.bin:cc10l.bin ld13.bin:ld13.bin; do
    cmp -s "tmp/s13/out2/${pair%%:*}" "tmp/build/${pair##*:}" || ok=1
done
[ "$ok" -eq 0 ]
report $? "same: ゲストが作り直した pp.bin / cc10l.bin / ld13.bin がチェーンの成果物とバイト一致"

cmp -s tmp/s13/out2/ccB.o tmp/s13/out2/cc12.o
report $? "fix: ゲスト内で作り直した cc が作る .o が元の cc の .o と一致 (固定点)"

summary
