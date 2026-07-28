#!/bin/bash
# Stage 4 テスト: sol の検証 (docs/stage004-sol.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: asm(sol.s) の SHA-256 が sol.md 記載値と一致
#   2. 実行: hello (文字列/ループ), fib (再帰/バッファ), ctrl (変数/if-else)
#   3. フィルタ: echo (getc/putc)
#   4. エラー系: 未定義語 -> 2, main 未定義 -> 3, 重複定義 -> 4,
#      制御不整合 -> 5, 前方参照 -> 6
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

sol=tmp/build/sol.bin
doc=stage004/sol.md

# 1. ビルド再現
ensure_build stage004
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$sol")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: asm(sol.s) の SHA-256 が sol.md 記載値と一致"

# 2. 実行テスト
run_case() {
    name=$1
    expect=$2
    sh tools/env.sh qemu "$sol" < "tests/stage004/$name.sol" > "tmp/$name.bin" \
        && sh tools/env.sh qemu "tmp/$name.bin" < /dev/null > "tmp/$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/$name.out")" = "$expect" ]
    report $? "run: $name.sol の実行"
}

run_case hello "hello"
run_case fib "55"
run_case ctrl "$(printf 'ABCDE\nYN')"

# 3. フィルタ
sh tools/env.sh qemu "$sol" < tests/stage004/echo.sol > tmp/echo.bin \
    && printf 'abc xyz.' | sh tools/env.sh qemu tmp/echo.bin > tmp/echo.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/echo.out)" = "abc xyz" ]
report $? "filter: echo.sol ('.' まで転写)"

# 4. エラー系
printf 'fn main foo 0 exit end .' | sh tools/env.sh qemu "$sol" > /dev/null
[ $? -eq 2 ]
report $? "error: 未定義の語で終了コード 2"

printf 'fn f 0 exit end .' | sh tools/env.sh qemu "$sol" > /dev/null
[ $? -eq 3 ]
report $? "error: main 未定義で終了コード 3"

printf 'var dup fn main 0 exit end .' | sh tools/env.sh qemu "$sol" > /dev/null
[ $? -eq 4 ]
report $? "error: プリミティブ語との重複定義で終了コード 4"

printf 'fn main then 0 exit end .' | sh tools/env.sh qemu "$sol" > /dev/null
[ $? -eq 5 ]
report $? "error: 制御構造の不整合で終了コード 5"

printf 'fn main x @ drop 0 exit end var x .' | sh tools/env.sh qemu "$sol" > /dev/null
[ $? -eq 6 ]
report $? "error: var の前方参照で終了コード 6"

summary
