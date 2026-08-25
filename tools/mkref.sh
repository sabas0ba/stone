#!/bin/sh
# mk20 と本物の make に **同じ Makefile** を読ませ，出る命令の並びを
# 突き合わせる (docs/stage017-cc.md 15.2)。
#
# 使用法:
#   mkref.sh tcc          tcc の Makefile で見る (第 3 部の 3 の 1 の完了条件)
#   mkref.sh <dir> [目標...]  任意の木で見る
#
# tcc の素材 (docs/external/tcc) は repo に入れない決まりなので，
# CI ではこの突き合わせは走らない (tests/stage017/test.sh は同じ形を
# 自前で並べた tests/stage017/mk/tccish.mk で見る)。**完了条件そのものは
# 本物の Makefile なので，手元で再現できる形にして残す**のがこの script の
# 役目である。
#
# 突き合わせるのは -n の出力である。$(MAKE) は「自分自身」なので綴りが
# 違う。そこだけ make へ揃えてから diff する。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/mkref
mkdir -p "$out"

command -v make > /dev/null 2>&1 || { echo "error: host に make が無い" >&2; exit 1; }
command -v gcc  > /dev/null 2>&1 || { echo "error: host に gcc が無い" >&2; exit 1; }

# mk20 をホストで走らせる身代わりを作る。**mk20.c そのもの**を取り込む
# ので，見ているのは OS 上のものと同じ読み手である
gcc -w -o "$out/mk20host" tests/stage017/host/mk20host.c
mk=$(CDPATH= cd -- "$out" && pwd)/mk20host

what=${1:-tcc}
shift 2> /dev/null || true

if [ "$what" = tcc ]; then
    src=docs/external/tcc
    [ -d "$src" ] || {
        echo "error: $src が無い (sh tools/fetch.sh tcc で取得できる)" >&2
        exit 1
    }
    dir=$out/tcc
    rm -rf "$dir"
    cp -r "$src" "$dir"
    # config.mak / config.h を作る。Makefile はこれを include するので，
    # 無いと読む前に止まる。**ホスト向けで良い** —— 見たいのは命令の
    # 並びであって，翻訳の結果ではない
    ( cd "$dir" && ./configure > ../configure.log 2>&1 )
    set --
else
    dir=$what
fi

( cd "$dir" && make -n --no-print-directory "$@" ) > "$out/ref" 2>&1 || true
( cd "$dir" && "$mk" -n "$@" ) > "$out/raw" 2>&1 || true
sed "s#$mk -n#make#g" "$out/raw" > "$out/got"

echo "make: $(wc -l < "$out/ref") 行 / mk20: $(wc -l < "$out/got") 行"
if diff -u "$out/ref" "$out/got"; then
    echo "一致した ($dir)"
else
    echo "食い違った ($dir)" >&2
    exit 1
fi
