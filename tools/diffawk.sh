#!/bin/sh
# 我々の awk とホストの awk に**同じ台本と同じ入力**を食わせ，出力と
# 終了コードを突き合わせる (docs/stage017-gcc.md 5.8)。
#
#   sh tools/diffawk.sh        ホストで組んだ我々の awk と突き合わせる
#   sh tools/diffawk.sh os     **我々の OS の上で走らせた** awk と突き合わせる
#
# ## なぜ要るか
#
# awk は言語ひとつぶんある。**我々が期待値を書くと，我々の読み違いが
# そのまま期待値になる** —— 値と文字列の二面性 (strnum)，数を字にする
# 形 (CONVFMT / OFMT)，欄を書き換えたときの $0 の組み直し方は，どれも
# 文言だけでは決まらない。物差しはホストの awk である (5.3 / 5.5 と
# 同じ筋)。
#
# ## 台本はファイルに置く
#
# 引数ではなく `-f` で渡す。**引用の差を測ってしまわないため**である。
#
# ## 順序の決まらない形は置かない
#
# `for (k in a)` の巡回順は awk の実装ごとに違ってよい (POSIX)。順序に
# 依る台本を置くと，差ではないものが差として出るので，和を取るなど
# 順序に依らない形だけを並べてある。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

mode=${1:-host}
out=tmp/dawk
rm -rf "$out"
mkdir -p "$out/root"

OURS=${STONE_AWK:-tmp/awkhost}
HOSTAWK=${HOSTAWK:-awk}

# ---- 入力 ----
cat > "$out/root/in1.txt" <<'EOF'
a b c
1 2 3
x y z
EOF

cat > "$out/root/in2.txt" <<'EOF'
alpha:1:x
beta:22:y
gamma:333:z
EOF

cat > "$out/root/in3.txt" <<'EOF'
  hello   world
10 9 0010
foo bar baz
-3 +4 1e3
EOF

# ---- 台本 ----
ncase=0
mkcase() {
    ncase=$((ncase + 1))
    n=$ncase
    [ "$n" -lt 10 ] && n="0$n"
    printf '%s\n' "$2" > "$out/root/a$n.awk"
    printf '%s a%s.awk\n' "$1" "$n" >> "$out/cases"
}

: > "$out/cases"

# ---- 記録と欄 ----
mkcase in1.txt '{ print }'
mkcase in1.txt '{ print $1 }'
mkcase in1.txt '{ print NF, $NF }'
mkcase in1.txt '{ print NR, $0 }'
mkcase in1.txt 'NR == 2'
mkcase in1.txt '/b/ { print }'
mkcase in1.txt '$1 ~ /^a/ { print $2 }'
mkcase in1.txt '!/b/ { print }'
mkcase in3.txt '{ print NF }'
mkcase in3.txt '{ print "[" $1 "]" }'
mkcase in2.txt 'BEGIN { FS = ":" } { print $2 }'
mkcase in2.txt 'BEGIN { FS = ":"; OFS = "-" } { $1 = $1; print }'
mkcase in1.txt '{ $2 = "X"; print; print NF }'
mkcase in1.txt '{ $5 = "Z"; print NF; print }'
mkcase in1.txt '{ NF = 2; print; print NF }'
mkcase in1.txt '{ $0 = "p q"; print NF, $2 }'
mkcase in1.txt '{ print $(NF - 1) }'

# ---- 数と字 ----
mkcase in1.txt 'BEGIN { print 1 / 3; print 2 / 3; print 1e10; print 1e-10 }'
mkcase in1.txt 'BEGIN { print 123456789; print 1234567890123; print 0.1 + 0.2 }'
mkcase in1.txt 'BEGIN { print 10 % 3, 2 ^ 10, -2 ^ 2, 7 / 2 }'
mkcase in1.txt 'BEGIN { print 100000, 1000000, 1e6 + 0.5 }'
mkcase in1.txt 'BEGIN { CONVFMT = "%.2g"; x = 3.14159; print (x "") }'
mkcase in1.txt 'BEGIN { OFMT = "%.2f"; print 3.14159 }'
mkcase in1.txt 'BEGIN { print 1 + 1 " " 2 * 2 }'
mkcase in1.txt 'BEGIN { print length(""), length("abc") }'
mkcase in1.txt 'BEGIN { print x + 0, "[" x "]", length(x) }'

# ---- printf ----
mkcase in1.txt 'BEGIN { printf "%d|%5d|%-5d|%05d|%+d|% d\n", 42, 42, 42, 42, 42, 42 }'
mkcase in1.txt 'BEGIN { printf "%x|%X|%o|%c|%c\n", 255, 255, 8, 65, "hi" }'
mkcase in1.txt 'BEGIN { printf "%s|%10s|%-10s|%.2s|\n", "abc", "abc", "abc", "abc" }'
mkcase in1.txt 'BEGIN { printf "%e|%E|%f|%g|%G\n", 1234.5, 1234.5, 1234.5, 1234.5, 0.00001234 }'
mkcase in1.txt 'BEGIN { printf "%.0f|%.1f|%.3f|%.10f\n", 2.5, 2.45, 1 / 3, 1 / 3 }'
mkcase in1.txt 'BEGIN { printf "%g|%g|%g|%g\n", 100000, 1000000, 0.0001, 0.00001 }'
mkcase in1.txt 'BEGIN { printf "%%|%s\n", "x" }'
mkcase in1.txt 'BEGIN { printf "%d %d\n", "12abc", "  7  " }'
mkcase in1.txt 'BEGIN { printf "%*d|%.*f\n", 6, 42, 2, 3.14159 }'
mkcase in1.txt 'BEGIN { s = sprintf("%03d-%s", 7, "z"); print s, length(s) }'
# 数を字にする形をまとめて測る。**1 件で数百の変換**を突き合わせる ——
# 桁寄せの丸めはここでしか出ない
mkcase in1.txt 'BEGIN {
  n = split("0 1 -1 0.5 2.5 1.25 3.14159 0.1 0.2 0.3 1e-5 1e5 123456789 0.000123456 1e15 1e-15 12345.6789 999999.5 0.0000999999", V, " ")
  for (i = 1; i <= n; i++) {
    x = V[i] + 0
    printf "%s|%g|%.3g|%.10g|%e|%.2e|%f|%.0f|%.9f|%d\n", V[i], x, x, x, x, x, x, x, x, x
  }
}'
mkcase in1.txt 'BEGIN { print 2 ^ 31, 2 ^ 53, -(2 ^ 31), 2 ^ 62 }'
mkcase in1.txt 'BEGIN { for (i = 1; i <= 20; i++) print i / 7, i / 3, i * 1.1 }'

# ---- 比較 (strnum) ----
mkcase in3.txt '{ print ($1 == 10), ($1 < $2), ($3 == 10) }'
mkcase in1.txt 'BEGIN { x = "10"; y = 9; print (x < y), (x + 0 < y) }'
mkcase in1.txt 'BEGIN { print (1 == 1), (1 != 2), (1 < 2), (2 <= 2), (3 > 2), (3 >= 4) }'
mkcase in1.txt 'BEGIN { print ("abc" < "abd"), ("B" < "a"), ("" == 0) }'
mkcase in1.txt 'BEGIN { print 1 && 0, 1 || 0, !0, !"", !"x" }'

# ---- 文字列の道具 ----
mkcase in1.txt 'BEGIN { print substr("hello world", 7), substr("hello", 2, 3) }'
# substr の開始位置が 1 未満の形は**ホストと分かれる**。POSIX の文言と
# gawk は「位置 m..m+n-1 のうち在るものだけ」を返すので substr("hello",0,3)
# は "he" だが，mawk は m を 1 に切り上げてから n 文字取るので "hel" に
# なる。ここには置かず，我々の値を tests/stage017 に直に書いてある (5.8)
mkcase in1.txt 'BEGIN { print substr("hello", 1, 3), substr("hello", 4, 100), substr("hello", 2) }'
mkcase in1.txt 'BEGIN { print index("abcabc", "ca"), index("abc", "d"), index("abc", "") }'
mkcase in1.txt 'BEGIN { print toupper("aBc") tolower("XyZ") }'
mkcase in1.txt 'BEGIN { s = "hello"; n = sub(/l/, "L", s); print n, s }'
mkcase in1.txt 'BEGIN { s = "hello"; n = gsub(/l/, "L", s); print n, s }'
mkcase in1.txt 'BEGIN { s = "aaa"; n = gsub(/a/, "[&]", s); print n, s }'
mkcase in1.txt 'BEGIN { s = "aaa"; n = gsub(/a/, "\\&", s); print n, s }'
mkcase in1.txt 'BEGIN { s = "abc"; n = gsub(/x*/, "-", s); print n, s }'
mkcase in1.txt '{ gsub(/[aeiou]/, "*"); print }'
mkcase in1.txt 'BEGIN { if (match("foobar", /o+/)) print RSTART, RLENGTH; print match("x", /y/), RSTART, RLENGTH }'
mkcase in1.txt 'BEGIN { n = split("a:b:c", A, ":"); print n, A[1], A[3] }'
mkcase in1.txt 'BEGIN { n = split("  a  b  ", A); print n, "[" A[1] "]", "[" A[2] "]" }'
mkcase in1.txt 'BEGIN { n = split("", A); print n }'
mkcase in1.txt 'BEGIN { n = split("a1b22c", A, /[0-9]+/); print n, A[1], A[2], A[3] }'
mkcase in1.txt 'BEGIN { print int(3.9), int(-3.9), int("12abc") }'

# ---- 制御構造 ----
mkcase in1.txt 'BEGIN { while (i < 3) { print i; i++ } }'
mkcase in1.txt 'BEGIN { do { print i; i++ } while (i < 3) }'
mkcase in1.txt 'BEGIN { for (i = 0; i < 5; i++) { if (i == 2) continue; if (i == 4) break; print i } }'
mkcase in1.txt '{ if (NR == 2) next; print }'
mkcase in1.txt '{ print; exit 3 }'
mkcase in1.txt 'NR == 1, NR == 2 { print "R:" $0 }'
mkcase in1.txt '/a/, /y/ { print "S:" $0 }'
mkcase in1.txt '{ print (NR == 2 ? "two" : "other") }'

# ---- 配列 ----
mkcase in1.txt 'BEGIN { x["a"] = 1; if ("a" in x) print "yes"; if (!("b" in x)) print "no" }'
mkcase in1.txt 'BEGIN { x[1, 2] = 3; if ((1, 2) in x) print x[1, 2] }'
mkcase in1.txt 'BEGIN { x["a"] = 1; delete x["a"]; print ("a" in x) }'
mkcase in1.txt '{ a[$1] = NR } END { s = 0; for (k in a) s = s + a[k]; print s }'
mkcase in1.txt '{ n[NR] = $1 } END { for (i = 1; i <= NR; i++) print i, n[i] }'

# ---- 関数 ----
mkcase in1.txt 'function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) }
BEGIN { print fact(10) }'
mkcase in1.txt 'function add(a, b,   c) { c = a + b; return c }
BEGIN { print add(1, 2), c }'
mkcase in1.txt 'function fill(arr) { arr["x"] = 1 }
BEGIN { fill(A); print A["x"] }'
mkcase in1.txt 'function loc(n,   t) { t[1] = n; return t[1] }
BEGIN { print loc(5), loc(6) }'
mkcase in1.txt 'function f(s) { return s s }
BEGIN { print f("ab") }'

# ---- getline と RS ----
mkcase in1.txt 'BEGIN { while ((getline l < "in1.txt") > 0) n++; print n, l }'
mkcase in1.txt 'BEGIN { getline l < "nosuchfile"; print (getline l < "nosuchfile") }'
mkcase in1.txt 'NR == 1 { getline; print "after:" $0 }'
mkcase in2.txt 'BEGIN { RS = ":" } { print NR, $0 }'
# `print (a, b)` は括弧の中が**並び**である。autoconf の生成する awk に
# 現れる形で，単なる括弧と読むと 1 つの値になってしまう
mkcase in1.txt 'BEGIN { print (1, 2); print (3); x[1] = 1; print ((1) in x) }'
mkcase in1.txt '{ printf ("%s-%s\n", $1, $2) }'
# 自分自身への代入。**右辺が左辺そのものを指す**ので，解放の順を
# 間違えると解放済みの領域を読む (欄と配列の両方で踏んだ)
mkcase in1.txt 'BEGIN { a["k"] = "v"; a["k"] = a["k"]; print a["k"] }
{ $1 = $1; $0 = $0; print }'

pass=0
fail=0

# ---- ホスト側の答 ----
while read -r inf prog; do
    ( cd "$out/root" && "$HOSTAWK" -f "$prog" < "$inf" ) \
        > "$out/host.$prog" 2> /dev/null
    echo "rc=$?" >> "$out/host.$prog"
done < "$out/cases"

cmpcase() {
    prog=$1
    if cmp -s "$out/ours.$prog" "$out/host.$prog"; then
        printf 'ok   %-10s %s\n' "$prog" "$(head -n 1 "$out/root/$prog")"
        pass=$((pass + 1))
        return 0
    fi
    printf 'FAIL %-10s %s\n' "$prog" "$(head -n 1 "$out/root/$prog")"
    diff -u "$out/host.$prog" "$out/ours.$prog" | sed -n '3,12p' | sed 's/^/       /'
    fail=$((fail + 1))
    return 1
}

if [ "$mode" = host ]; then
    # **対照はここで組む。** 手順を外に置くと，走らせる人によって
    # 何を測ったかが変わる
    if [ -z "${STONE_AWK:-}" ]; then
        "${CC:-gcc}" -w -o "$OURS" stage017/awk1.c stage017/re2.c \
            stage017/awkfmt1.c \
            || { echo "error: $OURS を組めない (gcc が要る)" >&2; exit 1; }
    fi
    if [ ! -x "$OURS" ]; then
        echo "error: $OURS が無い" >&2
        exit 1
    fi
    ours=$(CDPATH= cd -- "$(dirname -- "$OURS")" && pwd)/$(basename -- "$OURS")
    while read -r inf prog; do
        ( cd "$out/root" && "$ours" -f "$prog" < "$inf" ) \
            > "$out/ours.$prog" 2> /dev/null
        echo "rc=$?" >> "$out/ours.$prog"
        cmpcase "$prog"
    done < "$out/cases"
else
    # ---- 我々の OS の上で走らせる ----
    for f in "${STONE_OSAWK:-tmp/build/awk1}" tmp/build/sh2.bin tmp/build/kernel24.bin; do
        [ -s "$f" ] || { echo "error: $f が無い (sh tools/build.sh stage017)" >&2; exit 1; }
    done
    cp "${STONE_OSAWK:-tmp/build/awk1}" "$out/root/awk"
    cp tmp/build/sh2.bin "$out/root/sh2"
    : > "$out/root/go.sh"
    while read -r inf prog; do
        printf 'echo @@%s\n' "$prog" >> "$out/root/go.sh"
        printf 'awk -f %s < %s\n' "$prog" "$inf" >> "$out/root/go.sh"
        printf 'echo rc=$?\n' >> "$out/root/go.sh"
    done < "$out/cases"
    printf 'echo @@end\n' >> "$out/root/go.sh"
    printf 'sh2 go.sh\n' > "$out/root/boot"

    sh tools/sfs3.sh pack "$out/root" "$out/fs.img" 16777216 256 > /dev/null \
        && rm -f "$out/ram" \
        && dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-1800} \
            STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
            > "$out/run.out" 2>&1
    if ! grep -q '^@@end$' "$out/run.out"; then
        echo "FAIL 走行が最後まで届かなかった ($out/run.out を見よ)"
        exit 1
    fi
    while read -r inf prog; do
        awk -v n="@@$prog" '
            $0 == n { on = 1; next }
            /^@@/   { on = 0 }
            on      { print }
        ' "$out/run.out" > "$out/ours.$prog"
        cmpcase "$prog"
    done < "$out/cases"
fi

echo
echo "diffawk ($mode): 一致 $pass / 食い違い $fail"
[ "$fail" -eq 0 ]
