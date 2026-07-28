#!/bin/bash
# Stage 10 テスト: C89 言語完成の検証 (docs/stage010-c89.md 7 章)。
#   第 1 部 = 文と式 (cc10a)，第 2 部の 1 = 型 (cc10b)，
#   第 2 部の 2 = 宣言 (cc10c)，第 2 部の 3 = 識別子と配列 (cc10d)，
#   第 2 部の 4 = 整数型 (cc10e)，第 2 部の 5 = 関数ポインタと static (cc10f)，
#   第 3 部の 1 = 初期化子 (cc10g)，第 3 部の 2 = 可変長引数 (cc10h)，
#   第 3 部の 3 = 構造体の値 (cc10i)
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
#      (第 3 部) 大域・局所の初期化子，初期値を持つ大域の .text 配置，
#      可変長引数 (include/stdarg.h を pp 経由で取り込む)，
#      構造体の値渡し・代入・局所の構造体変数・入れ子のメンバ
#   6. エラー系: 反復外の break -> 1, 未定義ラベルへの goto -> 2,
#      typedef と列挙定数の重複 -> 4, 未定義の struct タグ -> 2
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s10

cc=tmp/build/cc.bin        # 最新世代 (= cc10i.bin)
cca=tmp/build/cc10a.bin    # 第 1 部
ccb=tmp/build/cc10b.bin    # 第 2 部の 1
ccc=tmp/build/cc10c.bin    # 第 2 部の 2
ccd=tmp/build/cc10d.bin    # 第 2 部の 3
cce=tmp/build/cc10e.bin    # 第 2 部の 4
ccf=tmp/build/cc10f.bin    # 第 2 部の 5
ccg=tmp/build/cc10g.bin    # 第 3 部の 1
cch=tmp/build/cc10h.bin    # 第 3 部の 2
pp=tmp/build/pp.bin
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
for pair in cc10a:stage010/cc.md cc10b:stage010/cc2.md cc10c:stage010/cc3.md cc10d:stage010/cc4.md cc10e:stage010/cc5.md cc10f:stage010/cc6.md cc10g:stage010/cc7.md cc10h:stage010/cc8.md cc10i:stage010/cc9.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.bin"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: 各世代の SHA-256 が各 .md 記載値と一致"

# 2. セルフホストの健全性
cmp -s tmp/build/cc10a0.bin tmp/build/cc10a.bin
report $? "bootstrap: cc10a0.bin == cc10a.bin (第 1 部はコード生成が変わっていない)"
cmp -s tmp/build/cc10d0.bin tmp/build/cc10d.bin
report $? "bootstrap: cc10d0.bin == cc10d.bin (第 2 部の 3 もコード生成が変わっていない)"
cmp -s tmp/build/cc10g0.bin tmp/build/cc10g.bin
report $? "bootstrap: cc10g0.bin == cc10g.bin (第 3 部の 1 もコード生成が変わっていない)"
cmp -s tmp/build/cc10i0.bin tmp/build/cc10i.bin
report $? "bootstrap: cc10i0.bin == cc10i.bin (第 3 部の 3 もコード生成が変わっていない)"

# 3. 固定点
{ cat stage010/cc.sc; printf '\004'; } | sh tools/env.sh qemu "$cca" > tmp/s10/cc3.o \
    && link tmp/s10/cc3.bin tmp/s10/cc3.o && cmp -s tmp/s10/cc3.bin "$cca"
report $? "fixpoint: cc10a が自分自身を再生成する"

{ cat stage010/cc2.sc; printf '\004'; } | sh tools/env.sh qemu "$ccb" > tmp/s10/cc4.o \
    && link tmp/s10/cc4.bin tmp/s10/cc4.o && cmp -s tmp/s10/cc4.bin "$ccb"
report $? "fixpoint: cc10b が自分自身を再生成する"

{ cat stage010/cc3.sc; printf '\004'; } | sh tools/env.sh qemu "$ccc" > tmp/s10/cc5.o \
    && link tmp/s10/cc5.bin tmp/s10/cc5.o && cmp -s tmp/s10/cc5.bin "$ccc"
report $? "fixpoint: cc10c が自分自身を再生成する"

{ cat stage010/cc4.sc; printf '\004'; } | sh tools/env.sh qemu "$ccd" > tmp/s10/cc6.o \
    && link tmp/s10/cc6.bin tmp/s10/cc6.o && cmp -s tmp/s10/cc6.bin "$ccd"
report $? "fixpoint: cc10d が自分自身を再生成する"

{ cat stage010/cc5.sc; printf '\004'; } | sh tools/env.sh qemu "$cce" > tmp/s10/cc7.o \
    && link tmp/s10/cc7.bin tmp/s10/cc7.o && cmp -s tmp/s10/cc7.bin "$cce"
report $? "fixpoint: cc10e が自分自身を再生成する"

{ cat stage010/cc6.sc; printf '\004'; } | sh tools/env.sh qemu "$ccf" > tmp/s10/cc8.o \
    && link tmp/s10/cc8.bin tmp/s10/cc8.o && cmp -s tmp/s10/cc8.bin "$ccf"
report $? "fixpoint: cc10f が自分自身を再生成する"

{ cat stage010/cc7.sc; printf '\004'; } | sh tools/env.sh qemu "$ccg" > tmp/s10/cc9.o \
    && link tmp/s10/cc9.bin tmp/s10/cc9.o && cmp -s tmp/s10/cc9.bin "$ccg"
report $? "fixpoint: cc10g が自分自身を再生成する"

{ cat stage010/cc8.sc; printf '\004'; } | sh tools/env.sh qemu "$cch" > tmp/s10/cc10.o \
    && link tmp/s10/cc10.bin tmp/s10/cc10.o && cmp -s tmp/s10/cc10.bin "$cch"
report $? "fixpoint: cc10h が自分自身を再生成する"

compile stage010/cc9.sc tmp/s10/cc11.o && link tmp/s10/cc11.bin tmp/s10/cc11.o \
    && cmp -s tmp/s10/cc11.bin "$cc"
report $? "fixpoint: cc10i が自分自身を再生成する"

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
featcase mdarr
featcase ints
featcase init
featcase struct

# 可変長引数。va_list / va_start / va_arg は include/stdarg.h のマクロなので，
# pp を通してからコンパイルする
sh tools/bundle.sh include/stdarg.h $src/varg.c | sh tools/env.sh qemu "$pp" > tmp/s10/varg.i \
    && sh tools/env.sh qemu "$cc" < tmp/s10/varg.i > tmp/s10/varg.o \
    && link tmp/s10/varg.bin tmp/s10/varg.o \
    && sh tools/env.sh qemu tmp/s10/varg.bin < /dev/null > tmp/s10/varg.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s10/varg.out "$exp/varg.txt" > /dev/null
report $? "feature: varg (可変長引数)"

# 関数ポインタと static のリンケージ。2 翻訳単位が同名の static を持つ
compile $src/fnptr-a.c tmp/s10/fnptr-a.o \
    && compile $src/fnptr-b.c tmp/s10/fnptr-b.o \
    && link tmp/s10/fnptr.bin tmp/s10/fnptr-a.o tmp/s10/fnptr-b.o \
    && sh tools/env.sh qemu tmp/s10/fnptr.bin < /dev/null > tmp/s10/fnptr.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s10/fnptr.out "$exp/fnptr.txt" > /dev/null
report $? "feature: fnptr (関数ポインタ・static のリンケージ)"

# static がローカルシンボルとして出ていること (verify 層)
sh tools/env.sh run riscv64-unknown-elf-readelf -sW tmp/s10/fnptr-b.o > tmp/s10/fnptr.sym 2>&1 \
    && grep -q 'LOCAL .*hidden' tmp/s10/fnptr.sym \
    && grep -q 'GLOBAL .*fromother' tmp/s10/fnptr.sym
report $? "verify: static は LOCAL，非 static は GLOBAL で出る"

# 初期値を持つ大域は .text (節 1)，持たないものは .bss (節 2) にあること。
# 実体の置き場が設計どおりかは，値の一致だけでは確かめられない
sh tools/env.sh run riscv64-unknown-elf-readelf -sW tmp/s10/init.o > tmp/s10/init.sym 2>&1 \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +1 +gi$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +1 +gp$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +LOCAL +DEFAULT +1 +gstat$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +2 +ob$' tmp/s10/init.sym
report $? "verify: 初期値のある大域は .text，無い大域は .bss に出る"

# 宣言の共有 (プロトタイプ・extern・ブロック内宣言)。2 翻訳単位に分ける
compile $src/decl-a.c tmp/s10/decl-a.o \
    && compile $src/decl-b.c tmp/s10/decl-b.o \
    && link tmp/s10/decl.bin tmp/s10/decl-a.o tmp/s10/decl-b.o \
    && sh tools/env.sh qemu tmp/s10/decl.bin < /dev/null > tmp/s10/decl.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s10/decl.out "$exp/decl.txt" > /dev/null
report $? "feature: decl (プロトタイプ・extern・ブロック内宣言)"

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
errcase 5 'プロトタイプと定義で引数の個数が違う' 'int f(int a); int f(int a, int b) { return a + b; } int main() { return 0; }'
errcase 4 '関数の多重定義' 'int f() { return 0; } int f() { return 1; } int main() { return 0; }'
errcase 1 '局所の static 宣言は未対応' 'int main() { static int x; return 0; }'
errcase 1 '識別子が 31 バイトを超える' 'int main() { int abcdefghijabcdefghijabcdefghijab; return 0; }'
errcase 5 '関数でないものの間接呼出し' 'int main() { int a; a = 1; return a(1); }'
errcase 1 'extern 宣言に初期値' 'extern int x = 1; int main() { return 0; }'
errcase 6 '初期化子が要素数を超える' 'int a[2] = {1, 2, 3}; int main() { return 0; }'
errcase 1 '初期化子の区切りが不正' 'int a[2] = {1 2}; int main() { return 0; }'
errcase 1 '名前つき引数のない ...' 'int f(...); int main() { return 0; }'
errcase 1 'ピリオド 2 個' 'int f(int a, ..); int main() { return 0; }'
errcase 5 '可変長の呼出しに名前つきが足りない' 'int f(int a, int b, ...); int main() { return f(1); }'
errcase 5 '個数不明のまま呼んだ後に可変長と判る' 'int main() { return f(1, 2); } int f(int a, ...) { return a; }'
errcase 5 'プロトタイプと定義で可変長かどうかが違う' 'int f(int a, ...); int f(int a) { return a; } int main() { return 0; }'
errcase 5 '構造体の返却は未対応' 'struct S { int a; }; struct S f() { struct S s; return s; } int main() { return 0; }'
errcase 5 '自分自身をメンバに持つ構造体' 'struct S { struct S s; }; int main() { return 0; }'
errcase 5 '型の違う構造体の代入' 'struct A { int a; }; struct B { int b; }; struct A x; struct B y; int main() { x = y; return 0; }'
errcase 5 '可変長の可変部に構造体' 'struct S { int a; int b; }; int f(int n, ...); struct S s; int main() { return f(1, s); }'

summary
