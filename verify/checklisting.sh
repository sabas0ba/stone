#!/bin/sh
# verify 層: hex listing の注釈を，独立系の逆アセンブラ (binutils objdump)
# の出力と機械照合する。読取り専用の検証であり，ビルド成果物には影響
# しない (docs/plan.md 2.2)。
#
# 使用法: checklisting.sh <listing.hex> <flat.bin> [base]
#   例:   sh verify/checklisting.sh stage001/hex0.hex stage001/hex0.bin
#         sh verify/checklisting.sh stage002/hex1.hex tmp/build/hex1.bin 0x80000000
#
# base は listing のアドレス注釈に足す値 (既定 0)。hex0 は絶対アドレスで
# 書くので 0，hex1 のようにロード位置からのオフセットで書く listing は
# 0x80000000 を渡す。
#
# なぜ要るか (docs/dev-notes.md 3.3 の続き):
#   各 .md の SHA-256 は初回ビルドの自己記録であり，固定点検証も実行主体は
#   鎖の成果物そのものである。objdump による照合は，この自己参照性を補う
#   数少ない独立検査であり，「バイト列は正しいが listing の注釈だけが
#   ドリフトしている」ことを機械検出できる唯一の手段でもある。
#
# 照合するもの: 命令 1 語ごとの アドレス・ニーモニック・オペランド。
# 照合しないもの: `;` 以降の説明 (人間向けの散文であり機械照合できない)。
set -eu

[ $# -ge 2 ] && [ $# -le 3 ] \
    || { echo "usage: checklisting.sh <listing.hex> <flat.bin> [base]" >&2; exit 2; }
lst=$1
bin=$2
base=$(($(printf '%s' "${3:-0}")))
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

[ -f "$lst" ] || { echo "checklisting: no such listing: $lst" >&2; exit 2; }
[ -f "$bin" ] || { echo "checklisting: no such binary: $bin" >&2; exit 2; }

tmpd=$(mktemp -d)
trap 'rm -rf "$tmpd"' EXIT

# 数の表記ゆれを吸収する awk 関数群。listing は読みやすさで 16 進と 10 進を
# 使い分け，objdump は命令ごとに決め打ちで出す。どちらも符号つき 10 進へ
# 畳んでから比べる。
#
# 分岐・jal の飛び先は listing では `8000000c` と 0x なしで書くので，
# その位置だけは 16 進として読む (命令名で決まる)。
canon='
function hex2dec(s,   i, c, v, d) {
    v = 0
    for (i = 1; i <= length(s); i++) {
        c = tolower(substr(s, i, 1))
        d = index("0123456789abcdef", c) - 1
        if (d < 0) return "?" s
        v = v * 16 + d
    }
    return v
}
# 1 つの数トークンを 10 進へ。isaddr なら 0x 無しでも 16 進として読む
function num(t, isaddr,   neg) {
    neg = 0
    if (substr(t, 1, 1) == "-") { neg = 1; t = substr(t, 2) }
    if (substr(t, 1, 2) == "0x" || substr(t, 1, 2) == "0X") t = hex2dec(substr(t, 3))
    else if (isaddr) t = hex2dec(t)
    else if (t !~ /^[0-9]+$/) return (neg ? "-" : "") t
    return (neg ? -t : t+0)
}
# オペランド列を正規化する。mn は命令名 (飛び先の判定に使う)。
# ab は飛び先へ足す値 (listing がオフセットで書く場合。objdump 側は 0)
function ops(s, mn, ab,   n, a, i, o, isaddr) {
    gsub(/[ \t]+/, "", s)
    n = split(s, a, ",")
    o = ""
    for (i = 1; i <= n; i++) {
        # 最後のオペランドが飛び先になる命令
        isaddr = (i == n && (mn == "jal" || mn ~ /^b(eq|ne|lt|ge|ltu|geu)$/))
        if (a[i] ~ /^[-+]?(0[xX])?[0-9a-fA-F]+$/) a[i] = num(a[i], isaddr) + (isaddr ? ab : 0)
        else if (a[i] ~ /^[-+]?(0[xX])?[0-9a-fA-F]+\(x[0-9]+\)$/) {
            # 5(x5) の形。変位だけ畳む
            split(a[i], _p, "(")
            a[i] = num(_p[1], 0) "(" _p[2]
        }
        o = o (i > 1 ? "," : "") a[i]
    }
    return o
}
'

# --- listing 側: "bytes  # ADDR: MNEMONIC OPERANDS  ; 説明" を取り出す ---
awk -v base="$base" "$canon"'
/^[0-9a-fA-F][0-9a-fA-F ]*#[ \t]*[0-9a-fA-F]+:/ {
    p = index($0, "#")
    rest = substr($0, p + 1)
    c = index(rest, ";")                 # 人間向けの説明は落とす
    if (c > 0) rest = substr(rest, 1, c - 1)
    a = index(rest, ":")
    addr = rest; sub(/[ \t]*/, "", addr); addr = substr(rest, 1, a - 1)
    gsub(/[ \t]/, "", addr)
    body = substr(rest, a + 1)
    sub(/^[ \t]+/, "", body); sub(/[ \t]+$/, "", body)
    # 命令を主張していない注釈は飛ばす。listing は空き語を "(未使用)" の
    # ように括弧つきの注記で埋める (objdump はそれを c.unimp と読む)
    if (body == "" || substr(body, 1, 1) == "(") next
    sp = match(body, /[ \t]/)
    if (sp == 0) { mn = body; rst = "" } else { mn = substr(body, 1, sp - 1); rst = substr(body, sp + 1) }
    printf "%x %s %s\n", hex2dec(tolower(addr)) + base, tolower(mn), ops(tolower(rst), tolower(mn), base)
}
' "$lst" > "$tmpd/lst.txt"

# --- objdump 側 ---
sh "$repo_root/verify/disasm.sh" "$bin" > "$tmpd/raw.txt"
awk "$canon"'
/^[ \t]*[0-9a-fA-F]+:\t/ {
    line = $0
    h = index(line, "#")                 # objdump が付ける算出アドレスの注記
    if (h > 0) line = substr(line, 1, h - 1)
    a = index(line, ":")
    addr = substr(line, 1, a - 1); gsub(/[ \t]/, "", addr)
    rest = substr(line, a + 1)
    n = split(rest, f, "\t")             # \t 語 \t 命令 \t オペランド
    if (n < 3) next
    mn = f[3]; gsub(/[ \t]/, "", mn)
    opnd = (n >= 4) ? f[4] : ""
    sub(/[ \t]+$/, "", opnd)
    printf "%s %s %s\n", tolower(addr), tolower(mn), ops(tolower(opnd), tolower(mn), 0)
}
' "$tmpd/raw.txt" > "$tmpd/dis.txt"

nl=$(wc -l < "$tmpd/lst.txt" | tr -d ' \t')
nd=$(wc -l < "$tmpd/dis.txt" | tr -d ' \t')
[ "$nl" -gt 0 ] || { echo "checklisting: listing から命令が 1 つも読めない: $lst" >&2; exit 1; }

# listing に載っている各アドレスを逆アセンブル側と突き合わせる。
# listing がバイナリ全体を覆っていない場合もあるので，覆う範囲だけ比べ，
# 件数は最後に報告する。
join_out=$tmpd/diff.txt
: > "$join_out"
while read -r addr mn opnd; do
    d=$(grep -m1 "^$addr " "$tmpd/dis.txt" || true)
    if [ -z "$d" ]; then
        printf '%s: listing にあるが逆アセンブルに無い (%s %s)\n' "$addr" "$mn" "$opnd" >> "$join_out"
        continue
    fi
    dmn=$(printf '%s' "$d" | cut -d' ' -f2)
    dop=$(printf '%s' "$d" | cut -d' ' -f3-)
    if [ "$mn" != "$dmn" ] || [ "$opnd" != "$dop" ]; then
        printf '%s: listing "%s %s" / objdump "%s %s"\n' "$addr" "$mn" "$opnd" "$dmn" "$dop" >> "$join_out"
    fi
done < "$tmpd/lst.txt"

if [ -s "$join_out" ]; then
    echo "checklisting: $lst と $bin の逆アセンブルが食い違う:" >&2
    cat "$join_out" >&2
    exit 1
fi

echo "checklisting: $lst の注釈 $nl 命令が objdump の出力と一致 (逆アセンブル全体 $nd 命令)"
