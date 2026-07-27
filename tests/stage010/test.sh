#!/bin/bash
# Stage 10 テスト: C89 言語完成 第 1 部 (文と式) の検証
# (docs/stage010-c89.md 7 章)。
#
# テストの素材は用途で分ける。
#   src/       コンパイルして実行するプログラム
#   expected/  その標準出力
#
# 検証項目:
#   1. ビルド再現: cc.bin の SHA-256 が stage010/cc.md 記載値と一致
#   2. セルフホストの健全性: cc1.bin == cc.bin
#      (第 1 部はコード生成規則を変えないので，cc8 が作った 1 段目と
#       それが自分自身を再コンパイルしたものは一致しなければならない)
#   3. 固定点: cc.bin が自分自身を再生成する (B2 == B3)
#   4. 同値性: Stage 5 の仕様スイートと Stage 8 の分割コンパイル例が
#      新しい cc でも同じ結果になる (退行がないこと)
#   5. 新機能: for / do / switch / break / continue / goto / ?: /
#      複合代入 / ++ -- / カンマ / sizeof / キャストの実行結果
#   6. エラー系: 反復外の break -> 1, 未定義ラベルへの goto -> 2
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s10

cc=tmp/build/cc.bin
ld=tmp/build/ld.bin
src=tests/stage010/src
exp=tests/stage010/expected

compile() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$cc" > "$2"
}
link() {
    out=$1
    shift
    { cat "$@"; printf '\0'; } | sh tools/env.sh qemu "$ld" > "$out"
}

# 1. ビルド再現
sh tools/build.sh stage010 > /dev/null 2>&1
rc=$?
want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' stage010/cc.md | cut -d' ' -f2)
got=$(sha256sum tmp/build/cc.bin); got=${got%% *}
[ "$rc" -eq 0 ] && [ -n "$want" ] && [ "$want" = "$got" ]
report $? "build: cc.bin の SHA-256 が stage010/cc.md 記載値と一致"

# 2. セルフホストの健全性
cmp -s tmp/build/cc1.bin tmp/build/cc.bin
report $? "bootstrap: cc1.bin == cc.bin (コード生成が変わっていない)"

# 3. 固定点
compile stage010/cc.sc tmp/s10/cc3.o && link tmp/s10/cc3.bin tmp/s10/cc3.o \
    && cmp -s tmp/s10/cc3.bin "$cc"
report $? "fixpoint: cc が自分自身を再生成する"

# 4. 同値性 (Stage 5 の仕様スイート)
run_case() {
    name=$1
    input=$2
    expect=$3
    compile "$input" "tmp/s10/$name.o" \
        && link "tmp/s10/$name.bin" "tmp/s10/$name.o" \
        && sh tools/env.sh qemu "tmp/s10/$name.bin" < /dev/null > "tmp/s10/$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/s10/$name.out")" = "$expect" ]
    report $? "equiv: $name"
}
run_case arith tests/stage005/arith.sc "oooooooooooooooo"
run_case fib tests/stage005/fib.sc "610"
run_case ptr tests/stage005/ptr.sc "oooHI"
run_case struct tests/stage005/struct.sc "oopt"

compile tests/stage005/upper.sc tmp/s10/upper.o \
    && link tmp/s10/upper.bin tmp/s10/upper.o \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/s10/upper.bin > tmp/s10/upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s10/upper.out)" = "HELLO WORLD 123" ]
report $? "equiv: upper"

# 分割コンパイル (Stage 8 の例)
compile tests/stage008/split-a.sc tmp/s10/a.o \
    && compile tests/stage008/split-b.sc tmp/s10/b.o \
    && link tmp/s10/split.bin tmp/s10/a.o tmp/s10/b.o \
    && sh tools/env.sh qemu tmp/s10/split.bin < /dev/null > tmp/s10/split.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s10/split.out)" = "val=7val=10" ]
report $? "equiv: 分割コンパイル"

# 5. 新機能
featcase() {
    name=$1
    compile "$src/$name.c" "tmp/s10/$name.o" \
        && link "tmp/s10/$name.bin" "tmp/s10/$name.o" \
        && sh tools/env.sh qemu "tmp/s10/$name.bin" < /dev/null > "tmp/s10/$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && diff -q "tmp/s10/$name.out" "$exp/$name.txt" > /dev/null
    report $? "feature: $name"
}
featcase feat
featcase loops

# 6. エラー系
errcase() {
    want=$1
    label=$2
    input=$3
    printf '%s\004' "$input" | sh tools/env.sh qemu "$cc" > /dev/null 2>&1
    [ $? -eq "$want" ]
    report $? "error: $label (終了コード $want)"
}
errcase 1 '反復・switch の外の break' 'int main() { break; return 0; }'
errcase 1 '反復の外の continue' 'int main() { continue; return 0; }'
errcase 1 'switch の外の case' 'int main() { case 1: return 0; }'
errcase 2 '未定義のラベルへの goto' 'int main() { goto nowhere; return 0; }'
errcase 4 'ラベルの多重定義' 'int main() { a: a: return 0; }'
errcase 5 'ポインタへの *=' 'int main() { char *p; p = 0; p *= 2; return 0; }'
errcase 5 '左辺値でない対象への +=' 'int main() { int a; a = 1; 3 += a; return 0; }'

summary
