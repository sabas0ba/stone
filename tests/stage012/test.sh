#!/bin/bash
# Stage 12 テスト: 第 1 部 (共有領域と sfs)・第 2 部 (ld12 とカーネル) の検証
# (docs/stage012-os.md 8 章)。
#
#   往復        pack -> unpack がディレクトリを保存し，list が表を写す
#   注入と回収  ベアメタルのゲストプログラム (src/sfs1.c) が共有領域の sfs を
#               読み書きし，その結果をホスト側で回収して照合する
#   ビルド再現  ld12.bin / kernel.bin の SHA-256 が各 .md 記載値と一致する
#   退行        'F' 形式の出力が stage008 の ld とバイト一致する
#   OS          カーネルが sfs から ELF を読み，U モードで走らせ，
#               syscall を処理し，終了コードを返す
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s12

cc=tmp/build/cc.bin
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
ld12=tmp/build/ld12.bin
src=tests/stage012/src
usr=tests/stage012/user
exp=tests/stage012/expected
fix=tests/stage012/fixtures

RAMSIZE=134217728        # 128 MiB
SFSOFF=67108864          # 0x0400_0000 (ゲスト物理 0x8400_0000)
IMGSIZE=4194304          # 4 MiB

# ユーザプログラムを ELF 実行形式でビルドする。2 個目以降の引数は
# 一緒に並べるオブジェクト (libc)
builduser() {
    name=$1
    shift
    sh tools/bundle.sh stage012/libc/include/*.h "$usr/$name.c" \
        | sh tools/env.sh qemu "$pp" > "tmp/s12/$name.i" \
        && sh tools/env.sh qemu "$cc" < "tmp/s12/$name.i" > "tmp/s12/$name.o" \
        && { printf 'E'; cat "tmp/s12/$name.o" "$@"; printf '\0'; } \
            | sh tools/env.sh qemu "$ld12" > "tmp/s12/$name"
}

# root/ をイメージにして RAM ファイルへ埋め，カーネルを起動する
runos() {
    sh tools/sfs.sh pack tmp/s12/root tmp/s12/fs.img "$IMGSIZE" 128 || return 1
    rm -f tmp/s12/ram
    dd if=/dev/null of=tmp/s12/ram bs=1 seek="$RAMSIZE" 2> /dev/null
    dd if=tmp/s12/fs.img of=tmp/s12/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
        conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE=tmp/s12/ram sh tools/env.sh qemu tmp/build/kernel.bin \
        < /dev/null > "tmp/s12/$1.out"
}

# 実行後のイメージを回収して展開する
harvest() {
    dd if=tmp/s12/ram of=tmp/s12/fs2.img bs=64K iflag=skip_bytes,count_bytes \
        skip="$SFSOFF" count="$IMGSIZE" 2> /dev/null
    rm -rf tmp/s12/out2
    sh tools/sfs.sh unpack tmp/s12/fs2.img tmp/s12/out2
}

ensure_build stage012
buildrc=$?

# ---------------------------------------------------------------------------
section "sfs の往復 (ホスト側の道具)"

sh tools/sfs.sh pack "$fix" tmp/s12/fs.img "$IMGSIZE" 128
rm -rf tmp/s12/out
sh tools/sfs.sh unpack tmp/s12/fs.img tmp/s12/out
diff -r "$fix" tmp/s12/out > /dev/null
report $? "roundtrip: pack -> unpack がディレクトリを保存する"

sh tools/sfs.sh list tmp/s12/fs.img > tmp/s12/list.txt
diff -q tmp/s12/list.txt "$exp/list.txt" > /dev/null
report $? "list: 表の一覧が期待どおり"

# ---------------------------------------------------------------------------
section "ゲストからの読み書き (共有領域)"

{ cat "$src/sfs1.c"; printf '\004'; } | sh tools/env.sh qemu "$cc" > tmp/s12/sfs1.o \
    && { cat tmp/s12/sfs1.o; printf '\0'; } | sh tools/env.sh qemu "$ld" > tmp/s12/sfs1.bin
report $? "build: sfs1 (ベアメタル)"

rm -f tmp/s12/ram
dd if=/dev/null of=tmp/s12/ram bs=1 seek="$RAMSIZE" 2> /dev/null
dd if=tmp/s12/fs.img of=tmp/s12/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
    conv=notrunc 2> /dev/null
STONE_QEMU_RAMFILE=tmp/s12/ram sh tools/env.sh qemu tmp/s12/sfs1.bin \
    < /dev/null > tmp/s12/sfs1.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s12/sfs1.out "$exp/sfs1.txt" > /dev/null
report $? "guest: マジック確認と in.txt の読取り (UART 照合)"

harvest
printf 'written-by-guest\n' | diff -q - tmp/s12/out2/out.txt > /dev/null
report $? "guest: out.txt の作成がホストへ届く"
rm -f tmp/s12/out2/out.txt
diff -r "$fix" tmp/s12/out2 > /dev/null
report $? "guest: 既存ファイルが保存されている"

# ---------------------------------------------------------------------------
section "ld12: ビルド再現と 'F' の退行"

ok=0
[ "$buildrc" -eq 0 ] || ok=1
for pair in ld12:stage012/ld12.md kernel:stage012/kernel.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.bin"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: ld12.bin / kernel.bin の SHA-256 が各 .md 記載値と一致"

# 'F' 形式は stage008 の ld と同一の像を作る (前置部に触れていないことの証明)
ok=0
for o in pp.o cc10l.o ld.o; do
    { cat "tmp/build/$o"; printf '\0'; } | sh tools/env.sh qemu "$ld" > tmp/s12/f_old.bin
    r1=$?
    { printf 'F'; cat "tmp/build/$o"; printf '\0'; } | sh tools/env.sh qemu "$ld12" > tmp/s12/f_new.bin
    r2=$?
    if ! cmp -s tmp/s12/f_old.bin tmp/s12/f_new.bin; then
        # 落ちたときに何が起きたかを残す。無言だと CI で追えない
        echo "   $o: ld rc=$r1 ($(wc -c < tmp/s12/f_old.bin) バイト) /" \
             "ld12 rc=$r2 ($(wc -c < tmp/s12/f_new.bin) バイト)"
        ok=1
    fi
done
[ "$ok" -eq 0 ]
report $? "regress: 'F' の出力が stage008 の ld とバイト一致 (pp / cc10l / ld)"

# ---------------------------------------------------------------------------
section "OS: ユーザプログラムの実行と syscall"

builduser hello
report $? "build: hello (ELF 実行形式)"

# ELF として妥当であることを readelf で確かめる (verify 層)
sh tools/env.sh run riscv64-unknown-elf-readelf -h -l tmp/s12/hello > tmp/s12/hello.readelf 2>&1
grep -q 'EXEC (Executable file)' tmp/s12/hello.readelf \
    && grep -q 'RISC-V' tmp/s12/hello.readelf \
    && grep -q '0x86000054' tmp/s12/hello.readelf \
    && grep -q 'LOAD' tmp/s12/hello.readelf
report $? "elf: ET_EXEC / RISC-V / entry 0x86000054 / PT_LOAD"

rm -rf tmp/s12/root
mkdir -p tmp/s12/root
cp tmp/s12/hello tmp/s12/root/hello
printf 'hello\n' > tmp/s12/root/boot
runos hello
rc=$?
[ "$rc" -eq 3 ] && diff -q tmp/s12/hello.out "$exp/hello.txt" > /dev/null
report $? "run: hello が U モードで動き，argv と終了コード 3 が返る"

builduser io
report $? "build: io (ELF 実行形式)"

rm -rf tmp/s12/root
mkdir -p tmp/s12/root
cp tmp/s12/io tmp/s12/root/io
printf 'io\n' > tmp/s12/root/boot
printf 'abc\n' > tmp/s12/root/data.txt
runos io
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s12/io.out "$exp/io.txt" > /dev/null
report $? "run: io が read / openat / close / brk を通す (ENOENT / EBADF 含む)"

harvest
printf 'made\n' | diff -q - tmp/s12/out2/new.txt > /dev/null
report $? "run: io が作った new.txt がホストへ届く"

# ---------------------------------------------------------------------------
section "libc: 純粋部と環境部を OS の上で動かす (第 3 部)"

builduser libc tmp/build/l12_src_string.o tmp/build/l12_src_stdlib.o \
    tmp/build/l12_posix_sys.o tmp/build/l12_posix_morecore.o
report $? "build: libc (純粋部 string / stdlib + 環境部 sys / morecore)"

rm -rf tmp/s12/root
mkdir -p tmp/s12/root
cp tmp/s12/libc tmp/s12/root/libc
printf 'libc\n' > tmp/s12/root/boot
printf 'abc\n' > tmp/s12/root/data.txt
runos libc
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s12/libc.out "$exp/libc.txt" > /dev/null
report $? "run: 純粋部が無改変で動き，malloc が 1 MiB を超え，errno が立つ"

harvest
printf 'ok\n' | diff -q - tmp/s12/out2/out.txt > /dev/null
report $? "run: POSIX の write で作った out.txt がホストへ届く"

# ---------------------------------------------------------------------------
section "stdio: FILE と printf (第 4 部)"

builduser sio tmp/build/l12_src_string.o tmp/build/l12_src_stdlib.o tmp/build/l12_posix_sys.o \
    tmp/build/l12_posix_morecore.o tmp/build/l12_posix_stdio.o
report $? "build: sio (stdio を並べる)"

rm -rf tmp/s12/root
mkdir -p tmp/s12/root
cp tmp/s12/sio tmp/s12/root/sio
printf 'sio\n' > tmp/s12/root/boot
runos sio
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s12/sio.out "$exp/sio.txt" > /dev/null
report $? "run: printf の書式 (INT_MIN・幅・0 詰め) と FILE の往復・ungetc"

harvest
printf 'line1 5\nline2\n' | diff -q - tmp/s12/out2/sio.txt > /dev/null
report $? "run: fopen(\"w\") で書いたファイルがホストへ届く"

summary
