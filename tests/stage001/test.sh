#!/bin/bash
# Stage 1 テスト: hex0 の検証 (docs/stage001-hex0.md 6 章)。
#
# 検証項目:
#   1. hex0.bin の完全性 (hex0.md 記載の SHA-256 と一致)
#   2. 自己再生成: hex0(hex0.hex) == hex0.bin
#   3. 交差検証: hex0(hello.hex) == tests/stage000/hello.bin
#   4. エラー系: 不正文字 -> 1, 奇数桁 + 終端 -> 2
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

bin=stage001/hex0.bin
doc=stage001/hex0.md

# 1. 完全性
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$bin")
actual=${actual%% *}
[ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "integrity: hex0.bin が hex0.md 記載の SHA-256 と一致"

# 2. 自己再生成
sh tools/env.sh qemu "$bin" < stage001/hex0.hex > tmp/hex0-self.bin \
    && cmp -s tmp/hex0-self.bin "$bin"
report $? "self: hex0(hex0.hex) が hex0.bin とビット一致"

# 3. 交差検証
sh tools/env.sh qemu "$bin" < tests/stage001/hello.hex > tmp/hello-regen.bin \
    && cmp -s tmp/hello-regen.bin tests/stage000/hello.bin
report $? "cross: hex0(hello.hex) が hello.bin とビット一致"

# 4. エラー系
printf 'AB .' | sh tools/env.sh qemu "$bin" > /dev/null
[ $? -eq 1 ]
report $? "error: 不正文字 (大文字) で終了コード 1"

printf 'a.' | sh tools/env.sh qemu "$bin" > /dev/null
[ $? -eq 2 ]
report $? "error: 奇数桁 + 終端で終了コード 2"

summary
