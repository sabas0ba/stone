#!/bin/sh
# 我々の鎖とホストの処理系に**同じソースを**訳させ，走らせて
# **値を突き合わせる** (docs/stage017-gcc.md 5.2)。
#
#   sh tools/diff17.sh          プローブ全部
#   sh tools/diff17.sh <名前>   1 つだけ
#
# ## なぜ要るか
#
# `cc15u` (複合代入が符号を見ていない) は，**往復検査でも固定点でも
# 再現性でもバイト一致でも捕まらなかった**。捕まえたのは
# 「我々が書いていない物差し」だけである。
#
# 台帳 (tests/stage015/ledger.txt) の期待値は**我々が書いている**ので，
# 我々の思い込みがそのまま期待値になる。zlib の adler32 を突き合わせて
# 初めて出たのがその証拠である。**同じことを 1 回限りの手作業ではなく
# 仕組みにする**のがここである。
#
# ## 見るのは 2 通り
#
#   両方訳せた   標準出力を突き合わせる。違えば**我々の側を疑う**
#   我々が拒んだ ホストが**診断を出すか**を見る。出さないなら
#                「C が許す形を拒んでいる」ことになる
#
# 2 つ目が要るのは，台帳の `gap` に 2 つの意味があるからである ——
# 「まだ実装していない」と「誤った入力を正しく拒む」。後者を名乗るには
# **その入力が本当に誤っていること**を我々以外が言っている必要がある。
#
# ## ホストは万能の物差しではない
#
# 語長が違う (ホストは 64 bit)。C が定義していない振舞い (0 除算) は
# 比べようがない。鎖の内部だけの名前を呼ぶプローブもある。**飛ばす
# ものは名前と理由を必ず出す** —— 黙って飛ばすと「全部合った」に見える。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/d17
mkdir -p "$out"

cc=${STONE_DIFF_CC:-tmp/build/cc15v.bin}   # 最前線の世代で測る
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage015/probe
hdr=stage015/libc/include/stdarg.h
shim=tests/hostshim/shim.h

HOSTCC=${CC:-gcc}

# **飛ばすものと理由。** 黙って飛ばさない
skip_reason() {
    case $1 in
    fpsoft)
        echo "鎖の内部の名前 (__dadd / __dmul …) を直に呼ぶ。ホストに実体が無い" ;;
    layout-oracle)
        echo "main を持たない断片 (layout.c の期待値を出すためのもの)" ;;
    hyg16)
        echo "前処理器の検査。訳して走らせるものではない (既に gcc の cpp と突き合わせている)" ;;
    lldiv)
        echo "0 除算を見る。C が定義していない振舞いなのでホストは SIGFPE で落ちる" ;;
    strsizeof)
        echo "sizeof p == 4 を主張する。ホストは 64 bit なので語長で必ず違う" ;;
    strtod)
        echo "libc を繋ぎ OS の上で走らせる形。ここは前置部だけで走る形を見ている (**次に広げるならここ** —— ホストの strtod は良い物差しになる)" ;;
    layout)
        echo "構造体の配置を RV32 の規則で主張する。ホストは x86-64 の規則" ;;
    *)  echo "" ;;
    esac
}

pass=0; fail=0; skipped=0

one() {
    n=$1
    why=$(skip_reason "$n")
    if [ -n "$why" ]; then
        printf 'skip %-14s %s\n' "$n" "$why"
        skipped=$((skipped + 1))
        return 0
    fi

    # ---- 我々の側 ----
    ourc=0
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "$out/$n.i" 2> /dev/null || {
        printf 'FAIL %-14s 我々の pp が落ちた\n' "$n"
        fail=$((fail + 1)); return 1
    }
    sh tools/env.sh qemu "$cc" < "$out/$n.i" > "$out/$n.o" 2> /dev/null
    ourc=$?
    if [ "$ourc" -eq 0 ]; then
        { cat "$out/$n.o" tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
            | sh tools/env.sh qemu "$ld" > "$out/$n.bin" 2> /dev/null || {
            printf 'FAIL %-14s 我々の ld が落ちた\n' "$n"
            fail=$((fail + 1)); return 1
        }
        ourout=$(sh tools/env.sh qemu "$out/$n.bin" < /dev/null 2> /dev/null)
    fi

    # ---- ホストの側 ----
    "$HOSTCC" -w -include "$shim" -o "$out/h_$n" "$prb/$n.c" -lm \
        > "$out/h_$n.log" 2>&1
    hostc=$?

    if [ "$ourc" -ne 0 ]; then
        # **我々が拒んだ。** 拒むのが正しいと言えるのは，その入力が
        # 本当に C89 として誤っているときだけである。それを我々以外に
        # 言わせる。
        #
        # **警告の数を数えるのでは弱い。** gcc は正しいソースにも
        # -Wmissing-braces のような書き方の助言を出すので，
        # 「警告が出た = 誤った入力」にはならない。`-std=c89
        # -pedantic-errors` で**エラーになるか**を見る —— こちらは
        # 制約違反にしか出ない。
        #
        # なお -w は付けない。付けると診断そのものが消えて
        # 「ホストは何も言わなかった」という誤った結論になる
        "$HOSTCC" -std=c89 -pedantic-errors -include "$shim" \
            -fsyntax-only "$prb/$n.c" > "$out/p_$n.log" 2>&1
        pedc=$?
        if [ "$pedc" -ne 0 ]; then
            printf 'ok   %-14s 我々は拒む (rc=%s)。C89 として誤り (%s)\n' \
                "$n" "$ourc" \
                "$(grep -m1 -oE 'error: .*' "$out/p_$n.log")"
            pass=$((pass + 1)); return 0
        fi
        printf 'FAIL %-14s 我々だけが拒む (rc=%s)。ホストは C89 として通す\n' \
            "$n" "$ourc"
        fail=$((fail + 1)); return 1
    fi

    if [ "$hostc" -ne 0 ]; then
        printf 'FAIL %-14s 我々は通すがホストが翻訳できない (%s の先頭を見よ)\n' \
            "$n" "$out/h_$n.log"
        fail=$((fail + 1)); return 1
    fi

    hostout=$(timeout 30 "$out/h_$n" < /dev/null 2> /dev/null)
    hrc=$?
    if [ "$hrc" -ne 0 ]; then
        printf 'FAIL %-14s ホストの実行が rc=%s で落ちた\n' "$n" "$hrc"
        fail=$((fail + 1)); return 1
    fi

    if [ "$ourout" = "$hostout" ]; then
        printf 'ok   %-14s 値が一致 [%s]\n' "$n" "$ourout"
        pass=$((pass + 1)); return 0
    fi
    printf 'FAIL %-14s 値が違う\n' "$n"
    printf '       我々  [%s]\n' "$ourout"
    printf '       ホスト [%s]\n' "$hostout"
    fail=$((fail + 1)); return 1
}

if [ $# -gt 0 ]; then
    one "$1"
else
    for f in "$prb"/*.c; do
        one "$(basename "$f" .c)"
    done
fi

echo
echo "diff17: 一致 $pass / 食い違い $fail / 飛ばした $skipped"
[ "$fail" -eq 0 ]
