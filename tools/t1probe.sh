#!/bin/sh
# T1 と tccH に同じファイルを OS 上で翻訳させ，出た .o を突き合わせる。
#
#   sh tools/t1probe.sh <file.c>
#
# tccH はホストの交差 tcc が作った同じ tcc なので，食い違えば
# **我々の cc が T1 を誤訳した**ことになる (docs/stage015-tcc.md 12.18)。
# 1 ブートで両方を走らせるので 1 周 1〜2 分である。
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
out=tmp/s16
f=$1
b=$(basename "$f")
[ -d "$out/t2fs" ] || { echo "error: $out/t2fs が無い (tcc-stone.sh t2b)" >&2; exit 1; }
[ -f "$out/t2fs/tccH" ] || cp tmp/tcc/build/tccH "$out/t2fs/tccH"
cp "$f" "$out/t2fs/$b"
cat > "$out/t2fs/pboot" <<EOF
tcc1 -nostdinc -I/ -c $b -o p1.o
tccH -nostdinc -I/ -c $b -o ph.o
exit
EOF
printf 'sh\n' > "$out/t2fs/boot"
sh tools/sfs.sh pack "$out/t2fs" "$out/pfs.img" 8388608 256 > /dev/null
rm -f "$out/pram"
dd if=/dev/null of="$out/pram" bs=1 seek=134217728 2> /dev/null
dd if="$out/pfs.img" of="$out/pram" bs=64K oflag=seek_bytes \
    seek=67108864 conv=notrunc 2> /dev/null
{ cat "$out/t2fs/pboot"; printf '\004'; } \
    | STONE_QEMU_RAMFILE="$out/pram" sh tools/env.sh qemu \
        tmp/build/kernel16.bin
dd if="$out/pram" of="$out/pfs2.img" bs=64K iflag=skip_bytes \
    skip=67108864 count=256 2> /dev/null
rm -rf "$out/pout"
sh tools/sfs.sh unpack "$out/pfs2.img" "$out/pout" > /dev/null
if cmp -s "$out/pout/p1.o" "$out/pout/ph.o"; then
    echo "same: T1 と tccH の出力は一致" >&2
else
    echo "DIFF: T1=$(wc -c < "$out/pout/p1.o") tccH=$(wc -c < "$out/pout/ph.o")" >&2
    exit 1
fi
