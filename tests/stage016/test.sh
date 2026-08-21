#!/bin/bash
# Stage 16 の検査。設計は docs/stage016-os.md。
#
# 第 1 部はファイル系 (sfs2) である。ホスト側の道具だけで完結する部分を
# ここで固めてから，カーネルへ持ち込む。
set -u
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
out=tmp/s16t
rm -rf "$out"
mkdir -p "$out"

section "sfs2: ディレクトリを持つファイル系 (docs/stage016-os.md 6 章)"

# 木を組む。空ディレクトリ・空ファイル・3 段の入れ子・同名の別階層を含める
src=$out/tree
mkdir -p "$src/src/a/b" "$src/inc" "$src/empty"
echo "hello" > "$src/top.txt"
echo "aaa"   > "$src/src/one.c"
echo "bbb"   > "$src/src/a/two.c"
echo "ccc"   > "$src/src/a/b/three.c"
echo "ddd"   > "$src/inc/one.c"        # src/one.c と同名・別階層
: > "$src/inc/empty.h"

sh tools/sfs2.sh pack "$src" "$out/img" 1048576 64
report $? "pack: 木をイメージにできる"

[ "$(dd if="$out/img" bs=4 count=1 2> /dev/null)" = 'sfs2' ]
report $? "format: マジックが sfs2"

sh tools/sfs2.sh unpack "$out/img" "$out/back" > /dev/null
report $? "unpack: イメージから木を戻せる"

diff -r "$src" "$out/back" > /dev/null
report $? "roundtrip: pack -> unpack が木をそのまま保存する (空の階層も)"

# 一覧。ディレクトリは 'd'，ファイルは 'f' と長さ
sh tools/sfs2.sh list "$out/img" > "$out/list"
grep -q '^d .* src/a/b$' "$out/list" && grep -q '^f .* 4 src/a/b/three.c$' "$out/list" \
    || grep -q 'src/a/b/three.c' "$out/list"
report $? "list: 経路を親から組み立てて出す"

[ "$(grep -c '^d ' "$out/list")" -eq 5 ]
report $? "list: ディレクトリが 5 つ (src / src/a / src/a/b / inc / empty)"

[ "$(grep -c '^f ' "$out/list")" -eq 6 ]
report $? "list: ファイルが 6 つ"

# 同名・別階層が混ざらないこと (sfs1 は経路まるごとを名前にしていたので
# 起きなかったが，sfs2 は「親 + 名前」で引くのでここが要点になる)
[ "$(cat "$out/back/src/one.c")" = "aaa" ] && [ "$(cat "$out/back/inc/one.c")" = "ddd" ]
report $? "name: 同名で別階層のファイルが取り違えられない"

# 名前の長さの上限 (47 バイト)。48 バイトは拒む
long=$(printf 'a%.0s' $(seq 1 48))
mkdir -p "$out/bad"
: > "$out/bad/$long"
sh tools/sfs2.sh pack "$out/bad" "$out/bad.img" 65536 8 > /dev/null 2>&1
[ $? -ne 0 ]
report $? "limit: 48 バイトの名前を拒む (上限は 47)"

section "kernel17: カーネル側の経路解決 (docs/stage016-os.md 6 章)"

ensure_build stage016

# 検査用の木。中身をその経路の名札にしてあるので，取り違えれば中身で分かる
krt=$out/kroot
mkdir -p "$krt/src/a/b" "$krt/inc"
echo "TOP"     > "$krt/top.txt"
echo "SRC-ONE" > "$krt/src/one.c"
echo "INC-ONE" > "$krt/inc/one.c"      # src/one.c と同名・別階層
echo "THREE"   > "$krt/src/a/b/three.c"

# pathprobe を libc15 一式とリンクする ('E' 形式。kernel17 も読める)
sh tools/bundle.sh stage015/libc/include/*.h tests/stage016/user/pathprobe.c \
    2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/pathprobe.i" 2> /dev/null \
    && sh tools/env.sh qemu tmp/build/cc15p.bin < "$out/pathprobe.i" \
        > "$out/pathprobe.o" 2> /dev/null \
    && { printf 'E'; cat "$out/pathprobe.o" \
         tmp/build/l15_src_string.o tmp/build/l15_src_stdlib.o \
         tmp/build/l15_src_misc15.o tmp/build/l15_posix_sys.o \
         tmp/build/l15_posix_morecore.o tmp/build/l15_posix_stdio.o \
         tmp/build/l15_posix_assert.o tmp/build/rt64.o tmp/build/rtfp.o; \
         printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "$krt/pathprobe"
report $? "build: pathprobe を組める"

printf 'pathprobe\n' > "$krt/boot"
sh tools/sfs2.sh pack "$krt" "$out/kfs.img" 4194304 128 > /dev/null \
    && rm -f "$out/kram" \
    && dd if=/dev/null of="$out/kram" bs=1 seek=134217728 2> /dev/null \
    && dd if="$out/kfs.img" of="$out/kram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/kram" sh tools/env.sh qemu \
        tmp/build/kernel17.bin < /dev/null > "$out/pathprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/pathprobe.txt "$out/pathprobe.out" \
    > "$out/pathprobe.diff"
report $? "run: kernel17 が sfs2 の木を経路で引ける (絶対・相対・深さ・同名・不在)"
[ -s "$out/pathprobe.diff" ] && sed -n '4,$p' "$out/pathprobe.diff"

section "kernel18: ディレクトリの操作 (docs/stage016-os.md 7 章)"

# 検査用の木。中身をその経路の名札にしてある
drt=$out/droot
mkdir -p "$drt/src/a" "$drt/inc"
echo "TOP"     > "$drt/top.txt"
echo "SRC-ONE" > "$drt/src/one.c"
echo "A-TWO"   > "$drt/src/a/two.c"
echo "INC-ONE" > "$drt/inc/one.c"      # src/one.c と同名・別階層

# dirprobe を **libc16** とリンクする。libc15 とリンクしてはいけない ——
# libc15 の open は先頭の '/' を剥がすので，abs-from-src が黙って
# 間違う (docs/stage016-os.md 7.4)
sh tools/bundle.sh stage016/libc/include/*.h \
    "sys/stat.h=stage016/libc/include/sys/stat.h" \
    tests/stage016/user/dirprobe.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/dirprobe.i" 2> /dev/null \
    && sh tools/env.sh qemu tmp/build/cc15p.bin < "$out/dirprobe.i" \
        > "$out/dirprobe.o" 2> /dev/null \
    && { printf 'E'; cat "$out/dirprobe.o" \
         tmp/build/l16_src_string.o tmp/build/l16_src_stdlib.o \
         tmp/build/l16_src_misc15.o tmp/build/l16_posix_sys.o \
         tmp/build/l16_posix_morecore.o tmp/build/l16_posix_stdio.o \
         tmp/build/l16_posix_assert.o tmp/build/l16_posix_dir.o \
         tmp/build/rt64.o tmp/build/rtfp.o; \
         printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "$drt/dirprobe"
report $? "build: dirprobe を libc16 とリンクできる"

printf 'dirprobe\n' > "$drt/boot"
sh tools/sfs2.sh pack "$drt" "$out/dfs.img" 4194304 128 > /dev/null \
    && rm -f "$out/dram" \
    && dd if=/dev/null of="$out/dram" bs=1 seek=134217728 2> /dev/null \
    && dd if="$out/dfs.img" of="$out/dram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/dram" sh tools/env.sh qemu \
        tmp/build/kernel18.bin < /dev/null > "$out/dirprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/dirprobe.txt "$out/dirprobe.out" \
    > "$out/dirprobe.diff"
report $? "run: kernel18 が一覧・作成・移動と . / .. を扱える"
[ -s "$out/dirprobe.diff" ] && sed -n '4,$p' "$out/dirprobe.diff"

section "kernel19: 記憶域の拡張 (docs/stage016-os.md 8 章)"

# memprobe を libc16 とリンクする
mrt=$out/mroot
mkdir -p "$mrt"
sh tools/bundle.sh stage016/libc/include/*.h \
    tests/stage016/user/memprobe.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/memprobe.i" 2> /dev/null \
    && sh tools/env.sh qemu tmp/build/cc15p.bin < "$out/memprobe.i" \
        > "$out/memprobe.o" 2> /dev/null \
    && { printf 'E'; cat "$out/memprobe.o" \
         tmp/build/l16_src_string.o tmp/build/l16_src_stdlib.o \
         tmp/build/l16_src_misc15.o tmp/build/l16_posix_sys.o \
         tmp/build/l16_posix_morecore.o tmp/build/l16_posix_stdio.o \
         tmp/build/l16_posix_assert.o tmp/build/l16_posix_dir.o \
         tmp/build/rt64.o tmp/build/rtfp.o; \
         printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "$mrt/memprobe"
report $? "build: memprobe を組める"

printf 'memprobe\n' > "$mrt/boot"
sh tools/sfs2.sh pack "$mrt" "$out/mfs.img" 4194304 32 > /dev/null

# 同じ像を 2 つのカーネルで走らせて比べる。**片方だけを見ても
# 「広がった」ことは言えない**ので，古い世代の実測を対にして出す
memrun() {                  # memrun <kernel> <ramsize> <rambytes>
    rm -f "$out/mram"
    dd if=/dev/null of="$out/mram" bs=1 seek="$3" 2> /dev/null
    dd if="$out/mfs.img" of="$out/mram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE="$out/mram" STONE_QEMU_RAM="$2" \
        sh tools/env.sh qemu "tmp/build/$1.bin" < /dev/null 2>&1
}

old_out=$(memrun kernel18 128M 134217728)
old_mb=$(echo "$old_out" | sed -n 's/^got //p')
new_out=$(memrun kernel19 512M 536870912)
new_mb=$(echo "$new_out" | sed -n 's/^got //p')

echo "$old_out" | grep -q '^verify ok$'
report $? "kernel18 (128 MB): 取れた記憶域を書いて読み戻せる (got ${old_mb:-?} MiB)"

echo "$new_out" | grep -q '^verify ok$'
report $? "kernel19 (512 MB): 取れた記憶域を書いて読み戻せる (got ${new_mb:-?} MiB)"

# 旧世代は 14 MB の枠内 (像とデータスタックを引くので 14 未満)
[ -n "$old_mb" ] && [ "$old_mb" -lt 14 ]
report $? "kernel18 の上限は 14 MiB 未満 (UBRKMAX - UBASE = 14 MB)"

# 新世代は 250 MiB 以上。256 MB の枠から像とデータスタックを引いた値
[ -n "$new_mb" ] && [ "$new_mb" -ge 250 ]
report $? "kernel19 の上限は 250 MiB 以上 (got ${new_mb:-?} MiB)"

section "kernel20 / libc17: 削除と realpath (docs/stage016-os.md 9.4)"

# 検査用の木 (dirprobe と同じ形)
rrt=$out/rroot
mkdir -p "$rrt/src/a" "$rrt/inc"
echo "TOP"     > "$rrt/top.txt"
echo "SRC-ONE" > "$rrt/src/one.c"
echo "A-TWO"   > "$rrt/src/a/two.c"
echo "INC-ONE" > "$rrt/inc/one.c"

# rmprobe を **libc17** とリンクする。libc16 とリンクしてはいけない ——
# libc16 の unlink は何もせず 0 を返し，realpath は複写するだけなので，
# open-after と rp-rel が黙って間違う (docs/stage016-os.md 9.4)
sh tools/bundle.sh stage016/libc17/include/*.h \
    "sys/stat.h=stage016/libc17/include/sys/stat.h" \
    tests/stage016/user/rmprobe.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/rmprobe.i" 2> /dev/null \
    && sh tools/env.sh qemu tmp/build/cc15p.bin < "$out/rmprobe.i" \
        > "$out/rmprobe.o" 2> /dev/null \
    && { printf 'E'; cat "$out/rmprobe.o" \
         tmp/build/l17_src_string.o tmp/build/l17_src_stdlib.o \
         tmp/build/l17_src_misc15.o tmp/build/l17_posix_sys.o \
         tmp/build/l17_posix_morecore.o tmp/build/l17_posix_stdio.o \
         tmp/build/l17_posix_assert.o tmp/build/l17_posix_dir.o \
         tmp/build/rt64.o tmp/build/rtfp.o; \
         printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "$rrt/rmprobe"
report $? "build: rmprobe を libc17 とリンクできる"

printf 'rmprobe\n' > "$rrt/boot"
sh tools/sfs2.sh pack "$rrt" "$out/rfs.img" 4194304 128 > /dev/null \
    && rm -f "$out/rram" \
    && dd if=/dev/null of="$out/rram" bs=1 seek=536870912 2> /dev/null \
    && dd if="$out/rfs.img" of="$out/rram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/rram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel20.bin < /dev/null \
        > "$out/rmprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/rmprobe.txt "$out/rmprobe.out" \
    > "$out/rmprobe.diff"
report $? "run: kernel20 が消せて，libc17 の realpath が経路を畳める"
[ -s "$out/rmprobe.diff" ] && sed -n '4,$p' "$out/rmprobe.diff"

section "sh2: POSIX 部分集合のシェル (docs/stage016-os.md 10 章)"

# **期待値は本物の POSIX シェルで作る。** 我々のシェルはそれに一致
# しなければならない —— 我々が書いた期待値と突き合わせると，思い違いを
# そのまま固定してしまう
sh tests/stage016/user/shprobe.sh > "$out/shprobe.ref" 2>&1
report $? "ref: 参照シェルで probe が通る"

diff -q tests/stage016/expected/shprobe.txt "$out/shprobe.ref" > /dev/null
report $? "ref: 記録した期待値が参照シェルの出力と一致する"

# 各行は「名札 期待 実測」である。**expected/ との突き合わせだけでは，
# 記録した期待値のほうが間違っている場合を捕まえられない**
awk 'NF >= 3 && $2 != $3 { bad = 1 } END { exit bad }' \
    tests/stage016/expected/shprobe.txt
report $? "ref: 期待と実測が全行で一致している (記録の側の取り違え避け)"

srt=$out/sroot
mkdir -p "$srt"
cp tests/stage016/user/shprobe.sh "$srt/probe.sh"
cp tmp/build/sh2.bin "$srt/sh2"
printf 'sh2 probe.sh\n' > "$srt/boot"
sh tools/sfs2.sh pack "$srt" "$out/sfs.img" 4194304 64 > /dev/null \
    && rm -f "$out/sram" \
    && dd if=/dev/null of="$out/sram" bs=1 seek=536870912 2> /dev/null \
    && dd if="$out/sfs.img" of="$out/sram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/sram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel21.bin < /dev/null \
        > "$out/shprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/shprobe.txt "$out/shprobe.out" \
    > "$out/shprobe.diff"
report $? "run: sh2 が kernel21 の上で参照シェルと同じ結果を出す"
[ -s "$out/shprobe.diff" ] && sed -n '4,$p' "$out/shprobe.diff"

section "道具と空装置 (docs/stage016-os.md 11 章)"

# 道具の側も同じやり方で見る。**期待値は本物の sh と本物の coreutils
# で作る**ので，検査できるのは両者が一致する範囲だけである。一致
# しない範囲は 11.4 に不足として並べてある
sh tests/stage016/user/toolprobe.sh > "$out/toolprobe.ref" 2>&1
report $? "ref: 参照シェルと本物の道具で probe が通る"

diff -q tests/stage016/expected/toolprobe.txt "$out/toolprobe.ref" > /dev/null
report $? "ref: 記録した期待値が参照シェルの出力と一致する"

# 各行は「名札 期待 実測」である。期待と実測が食い違う行が 1 つでも
# あれば駄目。**expected/ との突き合わせだけでは，記録した期待値の
# ほうが間違っている場合を捕まえられない**
awk 'NF >= 3 && $2 != $3 { bad = 1 } END { exit bad }' \
    tests/stage016/expected/toolprobe.txt
report $? "ref: 期待と実測が全行で一致している (記録の側の取り違え避け)"

trt=$out/troot
mkdir -p "$trt"
cp tests/stage016/user/toolprobe.sh "$trt/probe.sh"
cp tmp/build/sh2.bin "$trt/sh2"
printf 'sh2 probe.sh\n' > "$trt/boot"
# 道具は木を作って回るので，項目数を多めに取る
sh tools/sfs2.sh pack "$trt" "$out/tsfs.img" 4194304 128 > /dev/null \
    && rm -f "$out/tram" \
    && dd if=/dev/null of="$out/tram" bs=1 seek=536870912 2> /dev/null \
    && dd if="$out/tsfs.img" of="$out/tram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/tram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel22.bin < /dev/null \
        > "$out/toolprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/toolprobe.txt "$out/toolprobe.out" \
    > "$out/toolprobe.diff"
report $? "run: 組込みの道具が kernel22 の上で本物と同じ結果を出す"
[ -s "$out/toolprobe.diff" ] && sed -n '4,$p' "$out/toolprobe.diff"

# uname だけは参照シェルで期待値を作れない。**我々の OS の素性を
# 答えるものだからである。** 並び順が本物と同じであることは，本物の
# uname が -m -s に sysname を先に返すのを見て決めた (11.3)
urt=$out/uroot
mkdir -p "$urt"
cp tests/stage016/user/unameprobe.sh "$urt/probe.sh"
cp tmp/build/sh2.bin "$urt/sh2"
printf 'sh2 probe.sh\n' > "$urt/boot"
sh tools/sfs2.sh pack "$urt" "$out/usfs.img" 1048576 32 > /dev/null \
    && rm -f "$out/uram" \
    && dd if=/dev/null of="$out/uram" bs=1 seek=536870912 2> /dev/null \
    && dd if="$out/usfs.img" of="$out/uram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null \
    && STONE_QEMU_RAMFILE="$out/uram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel22.bin < /dev/null \
        > "$out/unameprobe.out" 2>&1
r=$?
[ "$r" -eq 0 ] && diff -u tests/stage016/expected/unameprobe.txt \
    "$out/unameprobe.out" > "$out/unameprobe.diff"
report $? "run: uname が我々の素性を本物と同じ並び順で答える"
[ -s "$out/unameprobe.diff" ] && sed -n '4,$p' "$out/unameprobe.diff"

# 本物の uname が「書いた順ではなく決まった順」で並べることを，
# その場で確かめる。**これが崩れると上の期待値ごと嘘になる**
[ "$(uname -m -s)" = "$(uname -s) $(uname -m)" ]
report $? "ref: 本物の uname も -m -s に sysname を先に並べる"

summary
