#!/bin/bash
# Stage 12 テスト: 第 1 部 (共有領域と sfs) の検証 (docs/stage012-os.md 8 章)。
#
#   往復        pack -> unpack がディレクトリを保存し，list が表を写す
#   注入と回収  ベアメタルのゲストプログラム (src/sfs1.c) が共有領域の sfs を
#               読み書きし，その結果をホスト側で回収して照合する
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s12

cc=tmp/build/cc.bin
ld=tmp/build/ld.bin
src=tests/stage012/src
exp=tests/stage012/expected
fix=tests/stage012/fixtures

RAMSIZE=134217728        # 128 MiB
SFSOFF=67108864          # 0x0400_0000 (ゲスト物理 0x8400_0000)
IMGSIZE=4194304          # 4 MiB

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

ensure_build stage010

# ベアメタルのテストプログラム (pp は通さないので // コメントで書かれている)
{ cat "$src/sfs1.c"; printf '\004'; } | sh tools/env.sh qemu "$cc" > tmp/s12/sfs1.o \
    && { cat tmp/s12/sfs1.o; printf '\0'; } | sh tools/env.sh qemu "$ld" > tmp/s12/sfs1.bin
report $? "build: sfs1 (ベアメタル)"

# RAM ファイルを用意し，イメージを共有領域の位置へ埋め込む
rm -f tmp/s12/ram
dd if=/dev/null of=tmp/s12/ram bs=1 seek="$RAMSIZE" 2> /dev/null
dd if=tmp/s12/fs.img of=tmp/s12/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
    conv=notrunc 2> /dev/null

STONE_QEMU_RAMFILE=tmp/s12/ram sh tools/env.sh qemu tmp/s12/sfs1.bin \
    < /dev/null > tmp/s12/sfs1.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s12/sfs1.out "$exp/sfs1.txt" > /dev/null
report $? "guest: マジック確認と in.txt の読取り (UART 照合)"

# 書き戻されたイメージを回収して照合する
dd if=tmp/s12/ram of=tmp/s12/fs2.img bs=64K iflag=skip_bytes,count_bytes \
    skip="$SFSOFF" count="$IMGSIZE" 2> /dev/null
rm -rf tmp/s12/out2
sh tools/sfs.sh unpack tmp/s12/fs2.img tmp/s12/out2
printf 'written-by-guest\n' | diff -q - tmp/s12/out2/out.txt > /dev/null
report $? "guest: out.txt の作成がホストへ届く"
rm -f tmp/s12/out2/out.txt
diff -r "$fix" tmp/s12/out2 > /dev/null
report $? "guest: 既存ファイルが保存されている"

summary
