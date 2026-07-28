#!/bin/bash
# Stage 10 テスト: C89 言語完成の検証 (docs/stage010-c89.md 7 章)。
#
# Stage 10 は部に分かれていて，部ごとにコンパイラの世代がある。
# 一覧もその区切りでまとめる。どの部の何が落ちたのかが，出力を読むだけで判る。
#
#   共通         ビルド再現・退行 (Stage 5 の仕様スイート，Stage 8 の分割コンパイル)
#   第 1 部      文と式                    cc10a
#   第 2 部の 1  型                        cc10b
#   第 2 部の 2  宣言                      cc10c
#   第 2 部の 3  識別子と配列              cc10d
#   第 2 部の 4  整数型                    cc10e
#   第 2 部の 5  関数ポインタと static     cc10f
#   第 3 部の 1  初期化子                  cc10g
#   第 3 部の 2  可変長引数                cc10h
#   第 3 部の 3  構造体の値                cc10i
#
# 各部で見るもの:
#   固定点   その世代が自分自身を再生成する (B2 == B3)
#   bootstrap  コード生成規則を変えていない部では，前の世代が作った 1 段目と
#              正本がバイト単位で一致する
#   feature  その部で入れた機能を実行結果で確かめる
#   verify   値の一致では確かめられない配置を readelf で見る (検証層のみ)
#   error    その部で入れた診断が意図した終了コードを返す
#
# テストの素材は用途で分ける。
#   src/       コンパイルして実行するプログラム
#   expected/  その標準出力
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

# ある世代が自分自身を再生成することを見る (B2 == B3)
fixpoint() {
    gen=$1
    bin=$2
    srcfile=$3
    { cat "$srcfile"; printf '\004'; } | sh tools/env.sh qemu "$bin" > "tmp/s10/$gen.o" \
        && link "tmp/s10/$gen.bin" "tmp/s10/$gen.o" && cmp -s "tmp/s10/$gen.bin" "$bin"
    report $? "fixpoint: $gen が自分自身を再生成する"
}

# src/ のプログラムをコンパイル・リンク・実行し expected/ と照合する
featcase() {
    name=$1
    compile "$src/$name.c" "tmp/s10/$name.o" \
        && link "tmp/s10/$name.bin" "tmp/s10/$name.o" \
        && sh tools/env.sh qemu "tmp/s10/$name.bin" < /dev/null > "tmp/s10/$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && diff -q "tmp/s10/$name.out" "$exp/$name.txt" > /dev/null
    report $? "feature: $name"
}

errcase() {
    want=$1
    label=$2
    input=$3
    printf '%s\004' "$input" | sh tools/env.sh qemu "$cc" > /dev/null 2>&1
    [ $? -eq "$want" ]
    report $? "error: $label (終了コード $want)"
}

# ---------------------------------------------------------------------------
section "共通: ビルド再現"

ensure_build stage010
rc=$?
ok=0
[ "$rc" -eq 0 ] || ok=1
for pair in cc10a:stage010/cc.md cc10b:stage010/cc2.md cc10c:stage010/cc3.md \
            cc10d:stage010/cc4.md cc10e:stage010/cc5.md cc10f:stage010/cc6.md \
            cc10g:stage010/cc7.md cc10h:stage010/cc8.md cc10i:stage010/cc9.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.bin"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: 各世代の SHA-256 が各 .md 記載値と一致"

# ---------------------------------------------------------------------------
section "共通: 退行 (Stage 5 の仕様スイートと Stage 8 の分割コンパイル)"

# 生成物の名前は eq- を前置する。feature 側と同じ名前の題材があるので
# (Stage 5 の struct と src/struct.c)，混ざらないようにする
run_case() {
    name=$1
    input=$2
    expect=$3
    compile "$input" "tmp/s10/eq-$name.o" \
        && link "tmp/s10/eq-$name.bin" "tmp/s10/eq-$name.o" \
        && sh tools/env.sh qemu "tmp/s10/eq-$name.bin" < /dev/null > "tmp/s10/eq-$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/s10/eq-$name.out")" = "$expect" ]
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

compile tests/stage008/split-a.sc tmp/s10/a.o \
    && compile tests/stage008/split-b.sc tmp/s10/b.o \
    && link tmp/s10/split.bin tmp/s10/a.o tmp/s10/b.o \
    && sh tools/env.sh qemu tmp/s10/split.bin < /dev/null > tmp/s10/split.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s10/split.out)" = "val=7val=10" ]
report $? "equiv: 分割コンパイル"

# ---------------------------------------------------------------------------
section "第 1 部: 文と式 (cc10a)"

# 構文と IR 構築だけで閉じる部なので，cc8 が作った 1 段目と正本は一致する
cmp -s tmp/build/cc10a0.bin tmp/build/cc10a.bin
report $? "bootstrap: cc10a0.bin == cc10a.bin (コード生成が変わっていない)"
fixpoint cc10a "$cca" stage010/cc.sc
featcase feat
featcase loops
errcase 1 '反復・switch の外の break' 'int main() { break; return 0; }'
errcase 1 '反復の外の continue' 'int main() { continue; return 0; }'
errcase 1 'switch の外の case' 'int main() { case 1: return 0; }'
errcase 2 '未定義のラベルへの goto' 'int main() { goto nowhere; return 0; }'
errcase 4 'ラベルの多重定義' 'int main() { a: a: return 0; }'
errcase 5 'ポインタへの *=' 'int main() { char *p; p = 0; p *= 2; return 0; }'
errcase 5 '左辺値でない対象への +=' 'int main() { int a; a = 1; 3 += a; return 0; }'

# ---------------------------------------------------------------------------
section "第 2 部の 1: 型 (cc10b)"

fixpoint cc10b "$ccb" stage010/cc2.sc
featcase types
errcase 4 'typedef 名の重複' 'typedef int T; typedef char T; int main() { return 0; }'
errcase 4 '列挙定数の重複' 'enum { A }; enum { A }; int main() { return 0; }'
errcase 2 '未定義の struct タグ' 'struct nosuch v; int main() { return 0; }'

# ---------------------------------------------------------------------------
section "第 2 部の 2: 宣言 (cc10c)"

fixpoint cc10c "$ccc" stage010/cc3.sc

# 宣言の共有 (プロトタイプ・extern・ブロック内宣言)。2 翻訳単位に分ける
compile $src/decl-a.c tmp/s10/decl-a.o \
    && compile $src/decl-b.c tmp/s10/decl-b.o \
    && link tmp/s10/decl.bin tmp/s10/decl-a.o tmp/s10/decl-b.o \
    && sh tools/env.sh qemu tmp/s10/decl.bin < /dev/null > tmp/s10/decl.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s10/decl.out "$exp/decl.txt" > /dev/null
report $? "feature: decl (プロトタイプ・extern・ブロック内宣言)"

errcase 5 'プロトタイプと定義で引数の個数が違う' 'int f(int a); int f(int a, int b) { return a + b; } int main() { return 0; }'
errcase 4 '関数の多重定義' 'int f() { return 0; } int f() { return 1; } int main() { return 0; }'
errcase 1 '局所の static 宣言は未対応' 'int main() { static int x; return 0; }'

# ---------------------------------------------------------------------------
section "第 2 部の 3: 識別子と配列 (cc10d)"

# 字句と型表現を広げるだけなので，ここもコード生成規則は変わらない
cmp -s tmp/build/cc10d0.bin tmp/build/cc10d.bin
report $? "bootstrap: cc10d0.bin == cc10d.bin (コード生成が変わっていない)"
fixpoint cc10d "$ccd" stage010/cc4.sc
featcase mdarr
errcase 1 '識別子が 31 バイトを超える' 'int main() { int abcdefghijabcdefghijabcdefghijab; return 0; }'

# ---------------------------------------------------------------------------
section "第 2 部の 4: 整数型 (cc10e)"

fixpoint cc10e "$cce" stage010/cc5.sc
featcase ints

# ---------------------------------------------------------------------------
section "第 2 部の 5: 関数ポインタと static のリンケージ (cc10f)"

fixpoint cc10f "$ccf" stage010/cc6.sc

# 2 翻訳単位が同名の static を持つ
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

errcase 5 '関数でないものの間接呼出し' 'int main() { int a; a = 1; return a(1); }'

# ---------------------------------------------------------------------------
section "第 3 部の 1: 初期化子 (cc10g)"

# 実体の置き場を変えるだけで，出す命令は変えていない
cmp -s tmp/build/cc10g0.bin tmp/build/cc10g.bin
report $? "bootstrap: cc10g0.bin == cc10g.bin (コード生成が変わっていない)"
fixpoint cc10g "$ccg" stage010/cc7.sc
featcase init

# 初期値を持つ大域は .text (節 1)，持たないものは .bss (節 2) にあること。
# 実体の置き場が設計どおりかは，値の一致だけでは確かめられない
sh tools/env.sh run riscv64-unknown-elf-readelf -sW tmp/s10/init.o > tmp/s10/init.sym 2>&1 \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +1 +gi$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +1 +gp$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +LOCAL +DEFAULT +1 +gstat$' tmp/s10/init.sym \
    && grep -qE 'OBJECT +GLOBAL +DEFAULT +2 +ob$' tmp/s10/init.sym
report $? "verify: 初期値のある大域は .text，無い大域は .bss に出る"

errcase 1 'extern 宣言に初期値' 'extern int x = 1; int main() { return 0; }'
errcase 6 '初期化子が要素数を超える' 'int a[2] = {1, 2, 3}; int main() { return 0; }'
errcase 1 '初期化子の区切りが不正' 'int a[2] = {1 2}; int main() { return 0; }'

# ---------------------------------------------------------------------------
section "第 3 部の 2: 可変長引数 (cc10h)"

fixpoint cc10h "$cch" stage010/cc8.sc

# va_list / va_start / va_arg は include/stdarg.h のマクロなので，
# pp を通してからコンパイルする
sh tools/bundle.sh include/stdarg.h $src/varg.c | sh tools/env.sh qemu "$pp" > tmp/s10/varg.i \
    && sh tools/env.sh qemu "$cc" < tmp/s10/varg.i > tmp/s10/varg.o \
    && link tmp/s10/varg.bin tmp/s10/varg.o \
    && sh tools/env.sh qemu tmp/s10/varg.bin < /dev/null > tmp/s10/varg.out
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s10/varg.out "$exp/varg.txt" > /dev/null
report $? "feature: varg (可変長引数)"

errcase 1 '名前つき引数のない ...' 'int f(...); int main() { return 0; }'
errcase 1 'ピリオド 2 個' 'int f(int a, ..); int main() { return 0; }'
errcase 5 '可変長の呼出しに名前つきが足りない' 'int f(int a, int b, ...); int main() { return f(1); }'
errcase 5 '個数不明のまま呼んだ後に可変長と判る' 'int main() { return f(1, 2); } int f(int a, ...) { return a; }'
errcase 5 'プロトタイプと定義で可変長かどうかが違う' 'int f(int a, ...); int f(int a) { return a; } int main() { return 0; }'

# ---------------------------------------------------------------------------
section "第 3 部の 3: 構造体の値 (cc10i)"

# 複写を既存の load / store へ展開するので，出す命令の種類は変わらない
cmp -s tmp/build/cc10i0.bin tmp/build/cc10i.bin
report $? "bootstrap: cc10i0.bin == cc10i.bin (コード生成が変わっていない)"
fixpoint cc10i "$cc" stage010/cc9.sc
featcase struct
errcase 5 '構造体の返却は未対応' 'struct S { int a; }; struct S f() { struct S s; return s; } int main() { return 0; }'
errcase 5 '自分自身をメンバに持つ構造体' 'struct S { struct S s; }; int main() { return 0; }'
errcase 5 '型の違う構造体の代入' 'struct A { int a; }; struct B { int b; }; struct A x; struct B y; int main() { x = y; return 0; }'
errcase 5 '可変長の可変部に構造体' 'struct S { int a; int b; }; int f(int n, ...); struct S s; int main() { return f(1, s); }'

summary
