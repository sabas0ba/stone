#!/bin/sh
# 我々の OS の道具 (sh3 の組込み) を，ホストの同名の道具と突き合わせる
# (docs/stage017-gcc.md 5.6)。
#
#   sh tools/difftool.sh
#
# ## なぜ要るか
#
# `grep` / `tr` / `expr` / `sort` / `wc` / `basename` / `dirname` は
# autoconf の `configure` が使う。**無いと configure は落ちるのでは
# なく，空の値を掴んで進む** —— それがいちばん悪い。
#
# だから「動いた」ではなく「**同じ値が出た**」で測る。物差しはホストの
# 道具で，我々が期待値を書かない (5.3 / 5.5 と同じ筋)。
#
# ## 引用は避ける
#
# 命令の並びはシェルを通るので，我々のシェルとホストのシェルで引用の
# 扱いが違えば，道具の差ではないものが差として出る。**引用の要らない
# 形だけ**を並べてある。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/dtool
rm -rf "$out"
mkdir -p "$out/root"

cat > "$out/root/t1.txt" <<'EOF'
hello world
# comment
ac_cv_prog_CC=gcc
abc
abbbc
ac
xyz
aab
abbc
x123y
EOF

cat > "$out/root/t2.txt" <<'EOF'
pear
apple
banana
apple
cherry
EOF

# 1 行 1 件。**引用の要らない形だけ** —— 唯一の例外が掛け算の `\*` で，
# 素の `*` は**両方のシェルが経路展開してしまう** (sh5 が glob を
# 持ったので，いまはどちらも同じ振舞いである。5.9)。展開そのものは
# tools/diffglob.sh で測るので，ここでは道具の差だけを見る
cat > "$out/cmds" <<'EOF'
grep ab*c t1.txt
grep ^ac t1.txt
grep -v ^# t1.txt
grep [0-9] t1.txt
grep \(a\)\1 t1.txt
tr abc xyz < t1.txt
tr -d aeiou < t1.txt
expr 3 + 4
expr 10 - 3
expr 6 \* 7
expr 10 / 3
expr 10 % 3
expr abcdef : abc
expr abcdef : a.*f
expr abcdef : a\(b*\)c
expr abcdef : xyz
basename /a/b/c.txt
basename /a/b/c.txt .txt
basename c.txt
dirname /a/b/c.txt
dirname c.txt
dirname /a
wc -l < t1.txt
wc -w < t1.txt
wc -c < t2.txt
wc -l t2.txt
sort t2.txt
sort -u t2.txt
sort < t2.txt
sort -u < t2.txt
grep b < t2.txt
head -2 < t2.txt
wc -l < t2.txt
cat < t2.txt
grep -E ab+c t1.txt
grep -E ab?c t1.txt
grep -E ab{2}c t1.txt
grep -E [0-9]+y t1.txt
egrep a.c t1.txt
expr aabcd : a*abc
EOF

pass=0
fail=0

# ---- ホスト側の答 ----
n=0
while read -r line; do
    n=$((n + 1))
    ( cd "$out/root" && eval "$line" ) > "$out/host.$n" 2> /dev/null
    echo "$?" >> "$out/host.$n"
done < "$out/cmds"

# ---- 我々の OS で走らせる ----
osh=${STONE_OSSH:-tmp/build/sh5}
for f in "$osh" tmp/build/kernel24.bin; do
    [ -s "$f" ] || { echo "error: $f が無い (sh tools/build.sh stage017)" >&2; exit 1; }
done
cp "$osh" "$out/root/sh3"
: > "$out/root/go.sh"
n=0
while read -r line; do
    n=$((n + 1))
    printf 'echo @@%s\n%s\necho $?\n' "$n" "$line" >> "$out/root/go.sh"
done < "$out/cmds"
printf 'echo @@end\n' >> "$out/root/go.sh"
printf 'sh3 go.sh\n' > "$out/root/boot"

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

n=0
while read -r line; do
    n=$((n + 1))
    awk -v m="@@$n" '
        $0 == m { on = 1; next }
        /^@@/   { on = 0 }
        on      { print }
    ' "$out/run.out" > "$out/ours.$n"
    if cmp -s "$out/ours.$n" "$out/host.$n"; then
        printf 'ok   %-3s %s\n' "$n" "$line"
        pass=$((pass + 1))
    else
        printf 'FAIL %-3s %s\n' "$n" "$line"
        diff -u "$out/host.$n" "$out/ours.$n" | sed -n '3,10p' | sed 's/^/       /'
        fail=$((fail + 1))
    fi
done < "$out/cmds"

echo
echo "difftool: 一致 $pass / 食い違い $fail"
[ "$fail" -eq 0 ]
