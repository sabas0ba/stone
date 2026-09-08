#!/bin/sh
# 我々の sed とホストの sed に**同じ台本と同じ入力**を食わせ，出力を
# 突き合わせる (docs/stage017-gcc.md 5.5)。
#
#   sh tools/diffsed.sh        ホストで組んだ我々の sed と突き合わせる
#   sh tools/diffsed.sh os     **我々の OS の上で走らせた** sed と突き合わせる
#
# ## なぜ要るか
#
# `sed` の値は「我々が正しいと思う値」では測れない。POSIX の文言は
# 短く，実際の振舞い (空に合う置換の進み方・組の控え方・番地の範囲の
# 閉じ方) は**実物と突き合わせないと決まらない**。差分試験を libc へ
# 広げたとき (5.3) と同じ筋である。
#
# ## 台本はファイルに置く
#
# 引数ではなく `-f` で渡す。**引用の差を測ってしまわないため**である
# —— 我々のシェルとホストのシェルで `;` や `{` の扱いが違えば，sed の
# 差ではないものが差として出る。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

mode=${1:-host}
out=tmp/dsed
rm -rf "$out"
mkdir -p "$out/root"

OURS=${STONE_SED:-tmp/sedhost}
HOSTSED=${HOSTSED:-sed}

# ---- 入力 ----
cat > "$out/root/in1.txt" <<'EOF'
hello world
  leading spaces
ac_cv_prog_CC=gcc
ac_cv_prog_CXX=g++
# comment line
foo=bar baz
aaa bbb aaa
x
EOF

cat > "$out/root/in2.txt" <<'EOF'
one
two
three
four
five
EOF

# ---- 台本 ----
# 1 件 = 入力・-n の有無・台本。**autoconf の configure が使う形**を選ぶ
ncase=0
mkcase() {
    ncase=$((ncase + 1))
    n=$ncase
    [ "$n" -lt 10 ] && n="0$n"
    printf '%s\n' "$3" > "$out/root/s$n.sed"
    printf '%s %s s%s.sed\n' "$1" "$2" "$n" >> "$out/cases"
}

: > "$out/cases"
mkcase in1.txt 0 's/hello/HELLO/'
mkcase in1.txt 0 's/o/0/'
mkcase in1.txt 0 's/o/0/g'
mkcase in1.txt 0 's/o/0/2'
mkcase in1.txt 0 's/aaa/X/g'
mkcase in1.txt 0 's/^/> /'
mkcase in1.txt 0 's/$/ <EOL>/'
mkcase in1.txt 0 's/^ *//'
mkcase in1.txt 0 's/  *$//'
mkcase in1.txt 0 's/[aeiou]/./g'
mkcase in1.txt 0 's/[^a-z ]/#/g'
mkcase in1.txt 0 's/[a-z][a-z]*/W/g'
mkcase in1.txt 0 's/\(a*\)b/[\1]/g'
mkcase in1.txt 0 's/\([a-z_]*\)=\(.*\)/\2 is \1/'
mkcase in1.txt 0 's/\(.\)\1/<\1\1>/g'
mkcase in1.txt 0 's/^\(ac_cv_[a-z_]*\)=\(.*\)$/\1 -> \2/'
mkcase in1.txt 0 's/world/[&]/'
mkcase in1.txt 0 's/world/\&/'
mkcase in1.txt 0 's/o/\n/g'
mkcase in1.txt 0 's/x*/-/g'
mkcase in1.txt 0 's/ *//g'
mkcase in1.txt 0 'y/abc/ABC/'
mkcase in2.txt 1 '2p'
mkcase in2.txt 1 '$p'
mkcase in2.txt 1 '2,4p'
mkcase in2.txt 0 '2d'
mkcase in2.txt 0 '2,4d'
mkcase in2.txt 1 '/three/p'
mkcase in2.txt 0 '/three/d'
mkcase in2.txt 1 '/two/,/four/p'
mkcase in2.txt 1 '/two/,$p'
mkcase in2.txt 0 '/two/!d'
mkcase in2.txt 1 '='
mkcase in2.txt 0 '2q'
mkcase in2.txt 1 '1{
p
p
}'
mkcase in2.txt 0 's/one/1/
s/two/2/'
mkcase in2.txt 1 's/two/2/p'
mkcase in2.txt 1 'N
P'
mkcase in2.txt 0 'n
d'
mkcase in2.txt 1 ':a
/three/{
p
b a2
}
b
:a2'

pass=0
fail=0

# ---- ホスト側の答を作る ----
while read -r inf q scr; do
    if [ "$q" = 1 ]; then
        "$HOSTSED" -n -f "$out/root/$scr" < "$out/root/$inf" \
            > "$out/host.$scr" 2> /dev/null
    else
        "$HOSTSED" -f "$out/root/$scr" < "$out/root/$inf" \
            > "$out/host.$scr" 2> /dev/null
    fi
done < "$out/cases"

cmpcase() {
    scr=$1
    if cmp -s "$out/ours.$scr" "$out/host.$scr"; then
        printf 'ok   %-10s %s\n' "$scr" "$(head -n 1 "$out/root/$scr")"
        pass=$((pass + 1))
        return 0
    fi
    printf 'FAIL %-10s %s\n' "$scr" "$(head -n 1 "$out/root/$scr")"
    diff -u "$out/host.$scr" "$out/ours.$scr" | sed -n '3,12p' | sed 's/^/       /'
    fail=$((fail + 1))
    return 1
}

if [ "$mode" = host ]; then
    if [ ! -x "$OURS" ]; then
        echo "error: $OURS が無い (gcc -w -o tmp/sedhost stage017/sed1.c)" >&2
        exit 1
    fi
    while read -r inf q scr; do
        if [ "$q" = 1 ]; then
            "$OURS" -n -f "$out/root/$scr" < "$out/root/$inf" \
                > "$out/ours.$scr" 2> /dev/null
        else
            "$OURS" -f "$out/root/$scr" < "$out/root/$inf" \
                > "$out/ours.$scr" 2> /dev/null
        fi
        cmpcase "$scr"
    done < "$out/cases"
else
    # ---- 我々の OS の上で走らせる ----
    #
    # 起動は 1 回だけ。1 つの像に台本と入力を詰め，シェル (sh2) に
    # 順に起動させて `@@名前` の行で切り分ける (tools/diff17.sh と同じ手)
    for f in tmp/build/sed1 tmp/build/sh2.bin tmp/build/kernel24.bin; do
        [ -s "$f" ] || { echo "error: $f が無い (sh tools/build.sh stage017)" >&2; exit 1; }
    done
    cp tmp/build/sed1 "$out/root/sed"
    cp tmp/build/sh2.bin "$out/root/sh2"
    : > "$out/root/go.sh"
    while read -r inf q scr; do
        printf 'echo @@%s\n' "$scr" >> "$out/root/go.sh"
        if [ "$q" = 1 ]; then
            printf 'sed -n -f %s < %s\n' "$scr" "$inf" >> "$out/root/go.sh"
        else
            printf 'sed -f %s < %s\n' "$scr" "$inf" >> "$out/root/go.sh"
        fi
    done < "$out/cases"
    printf 'echo @@end\n' >> "$out/root/go.sh"
    printf 'sh2 go.sh\n' > "$out/root/boot"

    sh tools/sfs3.sh pack "$out/root" "$out/fs.img" 16777216 256 > /dev/null \
        && rm -f "$out/ram" \
        && dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-900} \
            STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
            > "$out/run.out" 2>&1
    if ! grep -q '^@@end$' "$out/run.out"; then
        echo "FAIL 走行が最後まで届かなかった ($out/run.out を見よ)"
        exit 1
    fi
    while read -r inf q scr; do
        awk -v n="@@$scr" '
            $0 == n { on = 1; next }
            /^@@/   { on = 0 }
            on      { print }
        ' "$out/run.out" > "$out/ours.$scr"
        cmpcase "$scr"
    done < "$out/cases"
fi

echo
echo "diffsed ($mode): 一致 $pass / 食い違い $fail"
[ "$fail" -eq 0 ]
