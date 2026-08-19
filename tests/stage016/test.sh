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

summary
