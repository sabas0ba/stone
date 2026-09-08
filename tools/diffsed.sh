#!/bin/sh
# 我々の sed とホストの sed に**同じ台本と同じ入力**を食わせ，出力を
# 突き合わせる (docs/stage017-gcc.md 5.5)。
#
#   sh tools/diffsed.sh          全部
#   sh tools/diffsed.sh <番号>   1 つだけ
#
# ## なぜ要るか
#
# `sed` の値は「我々が正しいと思う値」では測れない。POSIX の文言は
# 短く，実際の振舞い (空に合う置換の進み方・組の控え方・番地の範囲の
# 閉じ方) は**実物と突き合わせないと決まらない**。差分試験を libc へ
# 広げたとき (5.3) と同じ筋である。
#
# ## どちらの sed を測るか
#
#   STONE_SED=tmp/sedhost   ホストの gcc で組んだ我々の sed (既定)
#   STONE_SED=<OS 側>       我々の OS の上で走らせたもの (tests/stage017)
#
# ホスト側で組んだものを既定にするのは，**同じソースだから**である ——
# 直しの往復はこちらで回し，OS の上での走行は tests/stage017 が見る。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/dsed
mkdir -p "$out"

OURS=${STONE_SED:-tmp/sedhost}
HOSTSED=${HOSTSED:-sed}

if [ ! -x "$OURS" ]; then
    echo "error: $OURS が無い (gcc -w -o tmp/sedhost stage017/sed1.c)" >&2
    exit 1
fi

# 入力の台本。**我々のソースが使う形ではなく，autoconf の configure が
# 使う形**を選んである —— 変数の差し替え・行の抜き出し・字の入替え
cat > "$out/in1.txt" <<'EOF'
hello world
  leading spaces
ac_cv_prog_CC=gcc
ac_cv_prog_CXX=g++
# comment line
foo=bar baz
aaa bbb aaa
x
EOF

cat > "$out/in2.txt" <<'EOF'
one
two
three
four
five
EOF

# 台本と入力の組。1 行 1 件で "入力|台本" の形にする
run_one() {
    n=$1
    inf=$2
    shift 2
    "$OURS" "$@" < "$inf" > "$out/ours.$n" 2> "$out/ours.$n.err"
    orc=$?
    "$HOSTSED" "$@" < "$inf" > "$out/host.$n" 2> "$out/host.$n.err"
    hrc=$?
    if [ "$orc" -ne "$hrc" ]; then
        printf 'FAIL %-3s 終了コードが違う (我々 %s / ホスト %s): %s\n' \
            "$n" "$orc" "$hrc" "$*"
        fail=$((fail + 1))
        return 1
    fi
    if cmp -s "$out/ours.$n" "$out/host.$n"; then
        printf 'ok   %-3s %s\n' "$n" "$*"
        pass=$((pass + 1))
        return 0
    fi
    printf 'FAIL %-3s 出力が違う: %s\n' "$n" "$*"
    diff -u "$out/host.$n" "$out/ours.$n" | sed -n '3,12p' | sed 's/^/       /'
    fail=$((fail + 1))
    return 1
}

pass=0
fail=0
want=${1:-}
n=0

case1() {
    n=$((n + 1))
    if [ -n "$want" ] && [ "$want" != "$n" ]; then return 0; fi
    _in=$1
    shift
    run_one "$n" "$out/$_in" "$@"
}

# ---- s の基本 ----
case1 in1.txt 's/hello/HELLO/'
case1 in1.txt 's/o/0/'
case1 in1.txt 's/o/0/g'
case1 in1.txt 's/o/0/2'
case1 in1.txt 's/aaa/X/g'
case1 in1.txt 's/^/> /'
case1 in1.txt 's/$/ <EOL>/'
case1 in1.txt 's/^ *//'
case1 in1.txt 's/  *$//'
# ---- 文字級 ----
case1 in1.txt 's/[aeiou]/./g'
case1 in1.txt 's/[^a-z ]/#/g'
case1 in1.txt 's/[a-z][a-z]*/W/g'
# ---- 組と後方参照 ----
case1 in1.txt 's/\(a*\)b/[\1]/g'
case1 in1.txt 's/\([a-z_]*\)=\(.*\)/\2 is \1/'
case1 in1.txt 's/\(.\)\1/<\1\1>/g'
case1 in1.txt 's/^\(ac_cv_[a-z_]*\)=\(.*\)$/\1 -> \2/'
# ---- & と逃げ ----
case1 in1.txt 's/world/[&]/'
case1 in1.txt 's/world/\&/'
case1 in1.txt 's/o/\n/g'
# ---- 空に合う形 ----
case1 in1.txt 's/x*/-/g'
case1 in1.txt 's/ *//g'
# ---- 番地 ----
case1 in2.txt -n '2p'
case1 in2.txt -n '$p'
case1 in2.txt -n '2,4p'
case1 in2.txt '2d'
case1 in2.txt '2,4d'
case1 in2.txt -n '/three/p'
case1 in2.txt '/three/d'
case1 in2.txt -n '/two/,/four/p'
case1 in2.txt -n '/two/,$p'
case1 in2.txt '/two/!d'
case1 in2.txt -n '='
case1 in2.txt '2q'
case1 in2.txt -n '1{p;p;}'
# ---- 複数の -e と ; ----
case1 in2.txt -e 's/one/1/' -e 's/two/2/'
case1 in2.txt 's/one/1/;s/two/2/'
# ---- y ----
case1 in1.txt 'y/abc/ABC/'
# ---- p 旗 ----
case1 in2.txt -n 's/two/2/p'
# ---- n と N ----
case1 in2.txt -n 'N;P'
case1 in2.txt 'n;d'
# ---- b と : ----
case1 in2.txt -n ':a;/three/{p;b a2;};b;:a2'

echo
echo "diffsed: 一致 $pass / 食い違い $fail"
[ "$fail" -eq 0 ]
