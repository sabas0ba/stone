#!/bin/bash
# Stage 7 テスト: occ (現代化) の検証 (docs/stage007-occ.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: B2 (occ.bin) の SHA-256 が occ.md 記載値と一致
#   2. 固定点: B3 = B2(occ.sc) が B2 とビット一致 (完了条件)
#   3. 同値性: Stage 5 仕様スイートを B2 でコンパイル・実行
#   4. パス単体テスト: fold / dce (コンパイル結果のビット一致), 出力サイズ改善
#   5. エラー系: 1..6
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

occ=tmp/build/occ.bin
doc=stage007/occ.md

csc() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$occ" > "$2"
}
cstr() {
    printf "$1\004" | sh tools/env.sh qemu "$occ" > "$2"
}

# 1. ビルド再現
ensure_build stage007
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$occ")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: B2 (occ.bin) の SHA-256 が occ.md 記載値と一致"

# 2. 固定点
csc stage007/occ.sc tmp/occ3.bin && cmp -s tmp/occ3.bin "$occ"
report $? "fixpoint: B3 = B2(occ.sc) が B2 とビット一致 (完了条件)"

# 3. 同値性: Stage 5 仕様スイート
run_case() {
    name=$1
    expect=$2
    csc "tests/stage005/$name.sc" "tmp/o7-$name.bin" \
        && sh tools/env.sh qemu "tmp/o7-$name.bin" < /dev/null > "tmp/o7-$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/o7-$name.out")" = "$expect" ]
    report $? "equiv: $name.sc (B2 でコンパイル)"
}

run_case arith "oooooooooooooooo"
run_case fib "610"
run_case ptr "oooHI"
run_case struct "oopt"

csc tests/stage005/upper.sc tmp/o7-upper.bin \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/o7-upper.bin > tmp/o7-upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/o7-upper.out)" = "HELLO WORLD 123" ]
report $? "equiv: upper.sc (B2 でコンパイル)"

# 4. パス単体テスト
cstr 'int main() { return 2 + 3 * 4; }' tmp/u1a.bin
cstr 'int main() { return 14; }' tmp/u1b.bin
cmp -s tmp/u1a.bin tmp/u1b.bin
report $? "fold: 2 + 3 * 4 が 14 と同一バイナリ"

cstr 'int main() { return -(2 + 3); }' tmp/u2a.bin
cstr 'int main() { return -5; }' tmp/u2b.bin
cmp -s tmp/u2a.bin tmp/u2b.bin
report $? "fold: -(2 + 3) が -5 と同一バイナリ"

cstr 'int main() { 1 + 2; return 7; }' tmp/u3a.bin
cstr 'int main() { return 7; }' tmp/u3b.bin
cmp -s tmp/u3a.bin tmp/u3b.bin
report $? "dce: 捨てられる式 1 + 2 の除去"

printf 'int main() { return 2 + 3 * 4; }\004' | sh tools/env.sh qemu tmp/build/scc.bin > tmp/u4.bin
[ "$(stat -c%s tmp/u1a.bin)" -lt "$(stat -c%s tmp/u4.bin)" ]
report $? "codegen: occ の出力が scc の出力より小さい"

# 5. エラー系
esc() {
    printf "$1\004" | sh tools/env.sh qemu "$occ" > /dev/null
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
