# sh2 の組込みの道具 (第 4 部の 3) を見る。
#
# shprobe.sh と同じく「名札 期待 実測」を 1 行 1 件で出し，突き合わせは
# expected/ が持つ。**期待値は本物の sh と本物の coreutils で作る**ので，
# ここに書けるのは本物と我々の実装が一致する範囲だけである。
#
# 一致しない範囲 (grep の正規表現，diff の差分本文，cp の複写先に
# ディレクトリを取る形など) は docs/stage016-os.md 11.4 に不足として
# 並べてある。**ここで検査しないことと，不足を隠すことは別である。**
#
# uname は我々の OS の素性を答えるものなので本物とは必ず食い違う。
# test.sh の側で別に見る。
#
# パイプも printf も使わない。どちらも sh2 に無い。
mkdir -p td
cd td

# ---- cat ----
echo one > a.txt
echo two > b.txt
cat a.txt > o1.txt
echo "cat one $(cat o1.txt)"
cat a.txt b.txt > o2.txt
echo "cat2 one $(head -1 o2.txt)"
cat < a.txt > o3.txt
echo "cat-stdin one $(cat o3.txt)"
cat nosuch.txt > /dev/null 2>&1 || echo "cat-missing ok ok"

# ---- head ----
cat > many.txt <<EOF
l1
l2
l3
l4
EOF
head -2 many.txt > h.txt
echo "head2 l1 $(head -1 h.txt)"
grep -q l2 h.txt && echo "head2-has-l2 ok ok"
grep -q l3 h.txt || echo "head2-cut-l3 ok ok"
head many.txt > h10.txt
diff many.txt h10.txt > /dev/null 2>&1 && echo "head-default ok ok"

# ---- grep ----
cat > g.txt <<EOF
alpha
beta
gamma
EOF
echo "grep beta $(grep beta g.txt)"
grep -q gamma g.txt && echo "grep-q ok ok"
grep -q delta g.txt || echo "grep-q-miss ok ok"
grep -v beta g.txt > gv.txt
echo "grep-v alpha $(head -1 gv.txt)"
cat > opt.txt <<EOF
-Wall
plain
EOF
grep -q -- -Wall opt.txt && echo "grep-dashdash ok ok"

# ---- diff ----
cp a.txt c.txt
diff a.txt c.txt > /dev/null 2>&1 && echo "diff-same ok ok"
diff a.txt b.txt > /dev/null 2>&1 || echo "diff-differ ok ok"

# ---- cp / mv / ln ----
cp a.txt cp1.txt
echo "cp one $(cat cp1.txt)"
mv cp1.txt mv1.txt
echo "mv one $(cat mv1.txt)"
cat cp1.txt > /dev/null 2>&1 || echo "mv-gone ok ok"
ln -sfn a.txt ln1.txt
echo "ln one $(cat ln1.txt)"

# ---- rm / mkdir ----
rm -f b.txt
cat b.txt > /dev/null 2>&1 || echo "rm ok ok"
rm -f nosuch2.txt && echo "rm-f ok ok"
mkdir -p deep/x/y
echo deep > deep/x/y/z.txt
echo "mkdir-p deep $(cat deep/x/y/z.txt)"

# ---- リダイレクトと組込みの誤り出力 ----
cat nosuch3.txt 2> /dev/null
echo "err-quiet ok ok"
cat nosuch4.txt 2> e.txt
# 文言は本物と違う。**「cat と名乗って何か言う」ところまでしか
# 揃えられない**ので，そこまでを見る
grep -q cat e.txt && echo "err-file ok ok"

cd ..
echo done
