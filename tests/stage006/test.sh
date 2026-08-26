#!/bin/bash
# Stage 6 テスト: scc (セルフホスト) の検証 (docs/stage006-scc.md 4 章)。
#
# 検証項目:
#   1. ビルド再現: B1 (scc1.bin)・B2 (scc.bin) の SHA-256 が scc.md 記載値と一致
#   2. 固定点: B3 = B2(scc.sc) が B2 とビット一致 (完了条件)。B1 == B2 も確認
#   3. 同値性: Stage 5 仕様スイートを B2 でコンパイル・実行し同じ結果になること
#   4. エラー系: B2 が Stage 5 と同じエラーコードを返すこと
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp
stable_dir=tmp/stable6

scc=tmp/build/scc.bin
doc=stage006/scc.md

csc() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$scc" > "$2"
}

# 1. ビルド再現 (B1 と B2 は同一のはずであり，記載値は 1 つ)
ensure_build stage006
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$scc")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: B2 (scc.bin) の SHA-256 が scc.md 記載値と一致"

# 2. 固定点検証
#
# **QEMU を通す実行は CI で稀に揺らぐ** (docs/dev-notes.md 1.6)。
# 素の cmp だと，中身の退行と実行の非再現が同じ FAIL に見える。
# stable_cmp は 2 度作らせて，同じものが 2 度出るなら退行として即座に
# 落とし，違うものが出るなら環境として数回やり直す
fp6gen() {
    { cat stage006/scc.sc; printf '\004'; } | sh tools/env.sh qemu "$scc" > "$1"
}
stable_cmp "fixpoint(scc)" fp6gen "$scc"
report $? "fixpoint: B3 = B2(scc.sc) が B2 とビット一致 (完了条件)"

cmp -s tmp/build/scc1.bin "$scc"
report $? "fixpoint: B1 = sc(scc.sc) が B2 とビット一致 (コード生成の同一性)"

# 3. 同値性: Stage 5 仕様スイート
run_case() {
    name=$1
    expect=$2
    csc "tests/stage005/$name.sc" "tmp/s6-$name.bin" \
        && sh tools/env.sh qemu "tmp/s6-$name.bin" < /dev/null > "tmp/s6-$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/s6-$name.out")" = "$expect" ]
    report $? "equiv: $name.sc (B2 でコンパイル)"
}

run_case arith "oooooooooooooooo"
run_case fib "610"
run_case ptr "oooHI"
run_case struct "oopt"

csc tests/stage005/upper.sc tmp/s6-upper.bin \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/s6-upper.bin > tmp/s6-upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s6-upper.out)" = "HELLO WORLD 123" ]
report $? "equiv: upper.sc (B2 でコンパイル)"

# 4. エラー系
esc() {
    printf "$1\004" | sh tools/env.sh qemu "$scc" > /dev/null
    [ $? -eq "$2" ]
    report $? "error: $3 で終了コード $2"
}
esc 'int main() { return 0 }' 1 "構文エラー"
esc 'int main() { return foo; }' 2 "未定義の識別子"
esc 'int f() { return 0; }' 3 "main 未定義"
esc 'int x; int x; int main() { return 0; }' 4 "重複定義"
esc 'int main() { 3 = 4; return 0; }' 5 "非左辺値への代入"
esc 'int main() { char b[3000]; return 0; }' 6 "フレーム容量超過"

summary
