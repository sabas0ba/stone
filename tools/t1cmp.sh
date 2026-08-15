#!/bin/sh
# 与えた .c を T1 と tccH の両方に OS 上で翻訳させ，出た .o を突き合わせる。
# 1 ブートで何本でも測れるので，切り分けの 1 周が 1〜2 分で回る。
#
#   sh tools/t1cmp.sh a.c b.c ...
#
# tccH はホストの交差 tcc が作った同じ tcc なので，食い違えば
# **我々の cc が T1 を誤訳した**ことになる (docs/stage015-tcc.md 12.18)。
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
out=tmp/s16
[ -d "$out/t2fs" ] || { echo "error: $out/t2fs が無い (tcc-stone.sh t2b)" >&2; exit 1; }
[ -f "$out/t2fs/tccH" ] || cp tmp/tcc/build/tccH "$out/t2fs/tccH"
: > "$out/t2fs/pboot"
i=0
names=""
for f in "$@"; do
    i=$((i + 1))
    cp "$f" "$out/t2fs/z$i.c"
    names="$names $i:$(basename "$f")"
    printf 'tcc1 -nostdinc -I/ -c z%s.c -o a%s.o\n' "$i" "$i" >> "$out/t2fs/pboot"
    printf 'tccH -nostdinc -I/ -c z%s.c -o b%s.o\n' "$i" "$i" >> "$out/t2fs/pboot"
done
printf 'exit\n' >> "$out/t2fs/pboot"
printf 'sh\n' > "$out/t2fs/boot"
sh tools/sfs.sh pack "$out/t2fs" "$out/pfs.img" 8388608 256 > /dev/null
rm -f "$out/pram"
dd if=/dev/null of="$out/pram" bs=1 seek=134217728 2> /dev/null
dd if="$out/pfs.img" of="$out/pram" bs=64K oflag=seek_bytes \
    seek=67108864 conv=notrunc 2> /dev/null
{ cat "$out/t2fs/pboot"; printf '\004'; } \
    | STONE_QEMU_RAMFILE="$out/pram" sh tools/env.sh qemu \
        tmp/build/kernel16.bin > /dev/null 2>&1 || true
dd if="$out/pram" of="$out/pfs2.img" bs=64K iflag=skip_bytes \
    skip=67108864 count=256 2> /dev/null
rm -rf "$out/pout"
sh tools/sfs.sh unpack "$out/pfs2.img" "$out/pout" > /dev/null
for nf in $names; do
    n=${nf%%:*}
    printf '%-24s ' "${nf#*:}"
    if [ ! -f "$out/pout/a$n.o" ] || [ ! -f "$out/pout/b$n.o" ]; then
        echo "(翻訳できず)"
    elif cmp -s "$out/pout/a$n.o" "$out/pout/b$n.o"; then
        echo "same"
    else
        ia=$(riscv64-unknown-elf-readelf -SW "$out/pout/a$n.o" \
             | grep -c '\.init_array' || true)
        echo "DIFF (T1=$(wc -c < "$out/pout/a$n.o") tccH=$(wc -c < "$out/pout/b$n.o") init_array=$ia)"
    fi
done
