#!/bin/bash
# Stage 9 テスト: プリプロセッサの検証 (docs/stage009-pp.md 6 章)。
#
# テストの素材は本スクリプトと同じ階層に置かず，用途で分ける。
#   src/       前処理にかける入力 (ソース・ヘッダ)
#   expected/  空白を畳んで照合する期待出力
#
# 検証項目:
#   1. ビルド再現: pp.bin の SHA-256 が stage009/pp.md 記載値と一致
#   2. 素通し: 指令を含まない実在のソースを pp に通しても生成物が変わらない
#   3. ヘッダの共有: 共通ヘッダを #include する 2 翻訳単位のビルドと実行
#   4. マクロ: 関数形式・入れ子・再帰抑止・# ・## ・可変長の展開結果
#   5. 条件: #if の算術と defined，#elif / #else，入れ子，抑止区間の無視
#   6. include: 入れ子・インクルードガード・<> 形式・__FILE__ / __LINE__
#   7. コメント: /* */ と // の除去，文字列中では解釈しないこと，行連結
#   8. エラー系: 対象なし -> 2, 引数個数 -> 3, 対応しない endif -> 4, #error -> 5
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s9
stable_dir=tmp/s9/stable

pp=tmp/build/pp.bin
cc=tmp/build/cc8.bin
ld=tmp/build/ld.bin
src=tests/stage009/src
exp=tests/stage009/expected

# 束ねを作らない素の入力 (入力全体を 1 メンバとみなす経路)
plain() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$pp"
}
# 束ねを与える入力 (最後のファイルが翻訳単位)
bundled() {
    sh tools/bundle.sh "$@" | sh tools/env.sh qemu "$pp"
}
# 空白の連なりを 1 個へ畳んで比較する (出力の改行位置は仕様ではない)
norm() {
    tr -d '\004' < "$1" | tr -s ' \t\n' ' ' | sed 's/^ *//; s/ *$//'
}
compile() {
    cat "$1" | sh tools/env.sh qemu "$cc" > "$2"
}
link() {
    out=$1
    shift
    { cat "$@"; printf '\0'; } | sh tools/env.sh qemu "$ld" > "$out"
}

# 1. ビルド再現
ensure_build stage009
rc=$?
want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' stage009/pp.md | cut -d' ' -f2)
got=$(sha256sum tmp/build/pp.bin); got=${got%% *}
[ "$rc" -eq 0 ] && [ -n "$want" ] && [ "$want" = "$got" ]
report $? "build: pp.bin の SHA-256 が stage009/pp.md 記載値と一致"

# 2. 素通し
# 指令を含まないソースは，pp を通してもコンパイル結果が変わってはならない。
# コメントの除去と行連結が意味を変えないことの検査でもある。
#
# **落ちたときに「中身が違う」のか「実行が再現していない」のかを
# 分ける** (docs/dev-notes.md 1.6)。この検査は CI で実際に揺らいだ
passthru() {
    input=$1
    ref=$2
    name=$3
    ptgen() {
        plain "$input" > "tmp/s9/$name.i" \
            && compile "tmp/s9/$name.i" "tmp/s9/$name.o" \
            && link "$1" "tmp/s9/$name.o"
    }
    stable_cmp "passthru($name)" ptgen "$ref"
    report $? "passthru: pp($name) -> cc8 -> ld が $name.bin と一致"
}
passthru stage008/cc.sc tmp/build/cc8.bin cc8
passthru stage008/ld.sc tmp/build/ld.bin ld
passthru stage009/pp.sc tmp/build/pp.bin pp

# 3. ヘッダの共有 (分割コンパイル)
bundled $src/shared.h $src/proga.c > tmp/s9/proga.i \
    && bundled $src/shared.h $src/progb.c > tmp/s9/progb.i \
    && compile tmp/s9/proga.i tmp/s9/proga.o \
    && compile tmp/s9/progb.i tmp/s9/progb.o \
    && link tmp/s9/prog.bin tmp/s9/proga.o tmp/s9/progb.o \
    && sh tools/env.sh qemu tmp/s9/prog.bin < /dev/null > tmp/s9/prog.out
rc=$?
# LIMIT = 30 + 6*2 = 42, DOUBLE(BASE) = 60。どちらも LIMIT 以上なので '!' が付く
[ "$rc" -eq 0 ] && [ "$(cat tmp/s9/prog.out)" = "v=!42 v=!60 " ]
report $? "header: 共通ヘッダを #include する 2 翻訳単位のビルドと実行"

# 4〜7. テキストの照合
textcase() {
    name=$1
    shift
    "$@" > "tmp/s9/$name.out" 2>/dev/null \
        && [ "$(norm "tmp/s9/$name.out")" = "$(norm "$exp/$name.txt")" ]
    report $? "text: $name"
}
textcase macro plain $src/macro.c
textcase cond plain $src/cond.c
textcase comment plain $src/comment.c
textcase incmain bundled $src/base.h $src/util.h $src/incmain.c

# 8. エラー系
errcase() {
    want=$1
    label=$2
    input=$3
    printf '%s\004' "$input" | sh tools/env.sh qemu "$pp" > /dev/null 2>&1
    [ $? -eq "$want" ]
    report $? "error: $label (終了コード $want)"
}
errcase 2 '#include の対象が無い' '#include "no-such.h"'
errcase 3 '実引数の個数不一致' '#define F(a,b) a
F(1);'
errcase 4 '対応しない #endif' '#endif'
errcase 4 '未終了の #if' '#if 1
x'
errcase 5 '#error' '#error boom'
errcase 1 '未知の指令' '#nosuch 1'
errcase 1 '未終端のブロックコメント' 'a /* unterminated'

# docs/stage009-pp.md 3.4 が明示する 2 つの規則。どちらも「そう決めた」
# 仕様なので，実装が偶然そうなっているだけの状態にしない
ifcase() {
    want=$1
    label=$2
    input=$3
    printf '%s\004' "$input" > tmp/s9/ifcase.in
    got=$(sh tools/env.sh qemu "$pp" < tmp/s9/ifcase.in 2> /dev/null \
        | tr -d '\004' | tr -s ' \t\n' ' ' | sed 's/^ *//; s/ *$//')
    [ "$got" = "$want" ]
    report $? "cond: $label (-> '$want')"
}

# 0 除算・剰余は 0 と定める (短絡しないので defined(N) && N > 3 のような
# 式で右辺が必ず評価されるため。同 3.4)
ifcase yes '0 による除算は 0' '#if 1 / 0 == 0
yes
#endif'
ifcase yes '0 による剰余は 0' '#if 1 % 0 == 0
yes
#endif'
# defined は括弧なしでも書ける
ifcase yes '括弧なしの defined' '#define N 1
#if defined N
yes
#endif'
ifcase yes '括弧なしの defined (未定義側)' '#if !defined N
yes
#endif'

summary
