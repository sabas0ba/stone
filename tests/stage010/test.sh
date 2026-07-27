#!/bin/bash
# Stage 10 テスト: C89 言語完成の検証 (docs/stage010-c89.md 7 章)。
#   第 1 部 = 文と式 (cc10a)，第 2 部の 1 = 型と宣言 (cc10b)
#
# テストの素材は用途で分ける。
#   src/       コンパイルして実行するプログラム
#   expected/  その標準出力
#
# 検証項目:
#   1. ビルド再現: cc10a.bin / cc10b.bin の SHA-256 が各 .md 記載値と一致
#   2. セルフホストの健全性: cc10a0.bin == cc10a.bin
#      (第 1 部はコード生成規則を変えないので，cc8 が作った 1 段目と
#       それが自分自身を再コンパイルしたものは一致しなければならない)
#   3. 固定点: 各世代が自分自身を再生成する (B2 == B3)
#   4. 同値性: Stage 5 の仕様スイートと Stage 8 の分割コンパイル例が
#      新しい cc でも同じ結果になる (退行がないこと)
#   5. 新機能: (第 1 部) for / do / switch / break / continue / goto / ?: /
#      複合代入 / ++ -- / カンマ / sizeof / キャスト
#      (第 2 部) typedef / enum / union / const・volatile / void / 大文字識別子
#   6. エラー系: 反復外の break -> 1, 未定義ラベルへの goto -> 2,
#      typedef と列挙定数の重複 -> 4, 未定義の struct タグ -> 2
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s10

cc=tmp/build/cc.bin        # 最新世代 (= cc10b.bin)
cca=tmp/build/cc10a.bin    # 第 1 部
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
ok=0
[ "$rc" -eq 0 ] || ok=1
for pair in cc10a:stage010/cc.md cc10b:stage010/cc2.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.bin"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc10a.bin / cc10b.bin の SHA-256 が各 .md 記載値と一致"

# 2. セルフホストの健全性
cmp -s tmp/build/cc10a0.bin tmp/build/cc10a.bin
report $? "bootstrap: cc10a0.bin == cc10a.bin (第 1 部はコード生成が変わっていない)"

# 3. 固定点
{ cat stage010/cc.sc; printf '\004'; } | sh tools/env.sh qemu "$cca" > tmp/s10/cc3.o \
    && link tmp/s10/cc3.bin tmp/s10/cc3.o && cmp -s tmp/s10/cc3.bin "$cca"
report $? "fixpoint: cc10a が自分自身を再生成する"

compile stage010/cc2.sc tmp/s10/cc4.o && link tmp/s10/cc4.bin tmp/s10/cc4.o \
    && cmp -s tmp/s10/cc4.bin "$cc"
report $? "fixpoint: cc10b が自分自身を再生成する"

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
featcase types

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
errcase 4 'typedef 名の重複' 'typedef int T; typedef char T; int main() { return 0; }'
errcase 4 '列挙定数の重複' 'enum { A }; enum { A }; int main() { return 0; }'
errcase 2 '未定義の struct タグ' 'struct nosuch v; int main() { return 0; }'

summary
