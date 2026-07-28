#!/bin/bash
# Stage 2 テスト: hex1 の検証 (docs/stage002-hex1.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: hex0(hex1.hex) の SHA-256 が hex1.md 記載値と一致
#   2. 上位互換: hex1(hex0.hex) == hex0.bin
#   3. 自己ビルド: hex1(hex1.hex) == hex1.bin
#   4. ラベル機能: 前方・後方参照 (!/$/&) が手計算版と一致し，生成物が実行できる
#   5. エラー系: 未定義ラベル -> 3, 重複定義 -> 4
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

hex0=stage001/hex0.bin
hex1=tmp/build/hex1.bin
doc=stage002/hex1.md

# 1. ビルド再現
ensure_build stage002
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$hex1")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: hex0(hex1.hex) の SHA-256 が hex1.md 記載値と一致"

# 2. 上位互換
sh tools/env.sh qemu "$hex1" < stage001/hex0.hex > tmp/hex0-via-hex1.bin \
    && cmp -s tmp/hex0-via-hex1.bin "$hex0"
report $? "compat: hex1(hex0.hex) が hex0.bin とビット一致"

# 3. 自己ビルド
sh tools/env.sh qemu "$hex1" < stage002/hex1.hex > tmp/hex1-self.bin \
    && cmp -s tmp/hex1-self.bin "$hex1"
report $? "self: hex1(hex1.hex) が hex1.bin とビット一致"

# 4. ラベル機能と生成物の実行
sh tools/env.sh qemu "$hex1" < tests/stage002/labels.hex1 > tmp/labels-h1.bin \
    && sh tools/env.sh qemu "$hex0" < tests/stage002/labels-expected.hex > tmp/labels-exp.bin \
    && cmp -s tmp/labels-h1.bin tmp/labels-exp.bin
report $? "labels: hex1(labels.hex1) がオフセット手計算版と一致"

sh tools/env.sh qemu tmp/labels-h1.bin < /dev/null > tmp/labels-run.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/labels-run.out)" = "ABCDE" ]
report $? "run: 生成物の実行 (stdout 'ABCDE', 終了コード 0)"

# 5. エラー系
printf '$nope 6f 00 00 00 .' | sh tools/env.sh qemu "$hex1" > /dev/null
[ $? -eq 3 ]
report $? "error: 未定義ラベルで終了コード 3"

printf ':a 00 :a 00 .' | sh tools/env.sh qemu "$hex1" > /dev/null
[ $? -eq 4 ]
report $? "error: 重複定義で終了コード 4"

summary
