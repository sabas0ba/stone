#!/bin/bash
# Stage 5 テスト: sc の検証 (docs/stage005-sc.md 5 章)。
# 言語仕様書 (同 2 章) に対するテストスイート。
#
# 検証項目:
#   1. ビルド再現: sol(sc.sol) の SHA-256 が sc.md 記載値と一致
#   2. 式・演算子 (arith.sc), 関数・再帰・前方参照 (fib.sc),
#      ポインタ・配列・文字列 (ptr.sc), 構造体 (struct.sc)
#   3. フィルタ (upper.sc)
#   4. エラー系: 構文 -> 1, 未定義 -> 2, main 未定義 -> 3, 重複 -> 4,
#      型 -> 5, 容量超過 -> 6
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

sc=tmp/build/sc.bin
doc=stage005/sc.md

# sc 入力の終端は EOT (0x04)。ソース末尾に付加して与える
csc() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$sc" > "$2"
}

# 1. ビルド再現
ensure_build stage005
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$sc")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: sol(sc.sol) の SHA-256 が sc.md 記載値と一致"

# 2. 仕様テストスイート (コンパイルして実行し出力を確認)
# 中間物は s5- を前置し，他 Stage のテストと衝突させない (tools/test.sh が
# Stage のテストを並列に走らせる)
run_case() {
    name=$1
    expect=$2
    csc "tests/stage005/$name.sc" "tmp/s5-$name.bin" \
        && sh tools/env.sh qemu "tmp/s5-$name.bin" < /dev/null > "tmp/s5-$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/s5-$name.out")" = "$expect" ]
    report $? "spec: $name.sc"
}

run_case arith "oooooooooooooooo"
run_case fib "610"
run_case ptr "oooHI"
run_case struct "oopt"

# 3. フィルタ
csc tests/stage005/upper.sc tmp/s5-upper.bin \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/s5-upper.bin > tmp/s5-upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s5-upper.out)" = "HELLO WORLD 123" ]
report $? "filter: upper.sc ('.' まで転写・大文字化)"

# 4. エラー系
esc() {
    printf "$1\004" | sh tools/env.sh qemu "$sc" > /dev/null
    [ $? -eq "$2" ]
    report $? "error: $3 で終了コード $2"
}
esc 'int main() { return 0 }' 1 "構文エラー (';' 欠落)"
esc 'int main() { return foo; }' 2 "未定義の識別子"
esc 'int f() { return 0; }' 3 "main 未定義"
esc 'int x; int x; int main() { return 0; }' 4 "重複定義"
esc 'int main() { 3 = 4; return 0; }' 5 "非左辺値への代入"
esc 'int main() { char b[3000]; return 0; }' 6 "フレーム容量超過"

summary
