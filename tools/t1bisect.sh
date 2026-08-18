#!/bin/sh
# 接頭辞を何本かまとめて 1 ブートで測る (t1probe.sh の多本版)。
#   sh tools/t1bisect.sh <src.c> <行数> [<行数> ...]
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
out=tmp/s16
srcf=$1; shift
: > "$out/t2fs/pboot"
for n in "$@"; do
    head -"$n" "$srcf" > "$out/t2fs/q$n.c"
    printf 'tcc1 -nostdinc -I/ -c q%s.c -o a%s.o\n' "$n" "$n" >> "$out/t2fs/pboot"
    printf 'tccH -nostdinc -I/ -c q%s.c -o b%s.o\n' "$n" "$n" >> "$out/t2fs/pboot"
done
printf 'exit\n' >> "$out/t2fs/pboot"
[ -f "$out/t2fs/tccH" ] || cp tmp/tcc/build/tccH "$out/t2fs/tccH"
printf 'sh\n' > "$out/t2fs/boot"
sh tools/sfs.sh pack "$out/t2fs" "$out/pfs.img" 8388608 256 > /dev/null
rm -f "$out/pram"
dd if=/dev/null of="$out/pram" bs=1 seek=134217728 2> /dev/null
dd if="$out/pfs.img" of="$out/pram" bs=64K oflag=seek_bytes \
    seek=67108864 conv=notrunc 2> /dev/null
{ cat "$out/t2fs/pboot"; printf '\004'; } \
    | STONE_QEMU_RAMFILE="$out/pram" sh tools/env.sh qemu \
        tmp/build/kernel16.bin > /dev/null 2>&1
dd if="$out/pram" of="$out/pfs2.img" bs=64K iflag=skip_bytes \
    skip=67108864 count=256 2> /dev/null
rm -rf "$out/pout"
sh tools/sfs.sh unpack "$out/pfs2.img" "$out/pout" > /dev/null
for n in "$@"; do
    if [ ! -f "$out/pout/a$n.o" ] || [ ! -f "$out/pout/b$n.o" ]; then
        echo "$n: (翻訳できず)"
    elif cmp -s "$out/pout/a$n.o" "$out/pout/b$n.o"; then
        echo "$n: same"
    else
        echo "$n: DIFF (T1=$(wc -c < "$out/pout/a$n.o") tccH=$(wc -c < "$out/pout/b$n.o"))"
    fi
done
