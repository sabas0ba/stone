#!/bin/sh
# 我々のシェルの経路展開 (glob) を，ホストのシェルと突き合わせる
# (docs/stage017-gcc.md 5.9)。
#
#   sh tools/diffglob.sh        ホストで組んだ我々のシェルと突き合わせる
#   sh tools/diffglob.sh os     **我々の OS の上で走らせた**シェルと突き合わせる
#
# ## なぜ要るか
#
# `sh2` は経路展開を持たなかった。tcc の `configure` には現れなかった
# からだが，GCC の `Makefile` と `configure` は `*.c` を使う。**無いと
# 落ちるのではなく，語がそのまま残って進む。**
#
# 展開の細目 (合うものが無いときの振舞い・先頭の `.` の扱い・並びの
# 順序・引用されたメタ文字・成分の途中にメタ文字がある形) は文言だけ
# では決まらないので，**同じ台本を両方のシェルに食わせて**測る。
#
# ## 台本はファイルに置く
#
# 引用の扱いそのものを測るので，引数で渡すと 2 重に引用を通ることに
# なる。ファイルに置いて両方に読ませる。
#
# ## 同じ一覧を見せる
#
# `echo *` は「その場に在るもの」を並べるので，**両方に同じ中身の
# ディレクトリを見せなければ**差ではないものが差として出る。台本と
# シェル本体を置いてから，ホスト側の答を採る。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

mode=${1:-host}
out=tmp/dglob
rm -rf "$out"
mkdir -p "$out/root/sub"

OURS=${STONE_SH:-tmp/sh5host}
# **対照はここで組む。** sh5.c は外部コマンドの起動を spawn2 (我々の
# カーネルの呼出し 501) に集めているので，ホストにはその実体が無い。
# tests/hostshim/shim-sh.c が fork / exec で同じ約束を与える ——
# 経路展開そのものはカーネルに依らないので，これで同じソースを
# 両方で走らせられる
HOSTSH=${HOSTSH:-sh}

# ---- 見せるもの ----
for f in a.txt b.txt ab.c xy.c; do : > "$out/root/$f"; done
for f in x.txt y.c; do : > "$out/root/sub/$f"; done

# **カーネルが見せる /dev/null に合わせる。** 我々の OS では像に入って
# いなくても `dev/null` が一覧に出るので，ホスト側にも同じものを置く。
# これはシェルの差ではなく OS の差である (置かないと `echo *` が
# 食い違い，シェルの差でないものを直しにいくことになる)
mkdir -p "$out/root/dev"
: > "$out/root/dev/null"

# ---- 台本 ----
# 1 行 1 件。`.` で始まる名前は測らない —— **我々の readdir は `.` と
# `..` を返さない** (dirent.h)。これはシェルの差ではなく OS の差である
cat > "$out/cmds" <<'EOF'
echo *.txt
echo *.c
echo *.zzz
echo a*
echo ?.txt
echo [ab].txt
echo [!a]*.txt
echo sub/*.txt
echo */y.c
echo *
echo "*.txt"
echo \*.txt
echo nomatch*/x
echo *.txt *.c
echo su?/*
echo sub/*
echo */*
echo x*y
echo *b*
echo ab*c
for f in *.txt; do echo "[$f]"; done
case a.txt in *.txt) echo case-ok ;; esac
v="*.txt"; echo $v
v="*.txt"; echo "$v"
EOF

# ---- 台本とシェルを置く (ホストにも同じ一覧を見せる) ----
: > "$out/root/g.sh"
n=0
while IFS= read -r line; do
    n=$((n + 1))
    printf 'echo @@%s\n%s\n' "$n" "$line" >> "$out/root/g.sh"
done < "$out/cmds"
printf 'echo @@end\n' >> "$out/root/g.sh"

if [ "$mode" = host ] && [ -z "${STONE_SH:-}" ]; then
    "${CC:-gcc}" -w -o "$OURS" stage017/sh5.c stage017/re2.c \
        tests/hostshim/shim-sh.c \
        || { echo "error: $OURS を組めない (gcc が要る)" >&2; exit 1; }
fi

if [ "$mode" = host ]; then
    [ -x "$OURS" ] || {
        echo "error: $OURS が無い" >&2
        exit 1
    }
    ours=$(CDPATH= cd -- "$(dirname -- "$OURS")" && pwd)/$(basename -- "$OURS")
    cp "$ours" "$out/root/sh5"
else
    [ -s tmp/build/sh5 ] || {
        echo "error: tmp/build/sh5 が無い (sh tools/build.sh stage017)" >&2
        exit 1
    }
    [ -s tmp/build/kernel24.bin ] || {
        echo "error: tmp/build/kernel24.bin が無い" >&2
        exit 1
    }
    cp tmp/build/sh5 "$out/root/sh5"
fi
printf 'sh5 g.sh\n' > "$out/root/boot"

# ---- ホスト側の答 ----
( cd "$out/root" && LC_ALL=C "$HOSTSH" g.sh ) > "$out/host.out" 2> /dev/null

# ---- 我々の側 ----
if [ "$mode" = host ]; then
    ( cd "$out/root" && ./sh5 g.sh ) > "$out/ours.out" 2> /dev/null
else
    # 像には dev を入れない —— カーネルが見せるものと二重になる
    rm -rf "$out/root/dev"
    sh tools/sfs3.sh pack "$out/root" "$out/fs.img" 16777216 256 > /dev/null \
        && rm -f "$out/ram" \
        && dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-600} \
            STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
            > "$out/run.out" 2>&1
    if ! grep -q '^@@end$' "$out/run.out"; then
        echo "FAIL 走行が最後まで届かなかった ($out/run.out を見よ)"
        exit 1
    fi
    sed -n '/^@@1$/,/^@@end$/p' "$out/run.out" > "$out/ours.out"
fi

pass=0
fail=0
n=0
while IFS= read -r line; do
    n=$((n + 1))
    for side in host ours; do
        awk -v m="@@$n" '
            $0 == m { on = 1; next }
            /^@@/   { on = 0 }
            on      { print }
        ' "$out/$side.out" > "$out/$side.$n"
    done
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
echo "diffglob ($mode): 一致 $pass / 食い違い $fail"
[ "$fail" -eq 0 ]
