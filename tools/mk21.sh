#!/bin/sh
# mk21 (stage019/mk21.c) の確かめの駆動 (docs/stage019-linux.md 10 章)。
#
#   mk21.sh host             ホスト版を tmp/mk21/mk21 に組む (MK_HOST)
#   mk21.sh stone <作業>     tests/stage019/mk の記述を stone の OS の上の mk21 で
#                            走らせ，記述ごとの出力を <作業>/<名前>.out に置く
#
# stone の上では命令は sh2 で走る。sh2 が > で結んだ出力を mk21 の子が継ぐと
# 書いた内容が残らないので，出力は端末に出して console から記述ごとに切り出す。
# uptodate.mk は 2 度走らせる (2 度目は何も作らない)。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
HOSTCC=${HOSTCC:-gcc}
t=tests/stage019/mk

die() { echo "mk21.sh: $*" >&2; exit 1; }

build_host() {
    mkdir -p tmp/mk21
    "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
        -D_XOPEN_SOURCE=700 -DMK_HOST -o tmp/mk21/mk21 stage019/mk21.c
    echo "built tmp/mk21/mk21" >&2
}

stone() {
    work=$1
    for f in mk21 sh2.bin kernel30.bin; do
        [ -s "tmp/build/$f" ] || die "stone の生成物が無い: tmp/build/$f (sh tools/build.sh stage019)"
    done
    rm -rf "$work"
    mkdir -p "$work/root/bin" "$work/root/t/src" "$work/root/t/out"
    cp tmp/build/mk21 "$work/root/mk21"
    cp tmp/build/sh2.bin "$work/root/bin/sh2"
    cp "$t"/*.mk "$t/in" "$work/root/t/"
    cp "$t"/src/* "$work/root/t/src/"
    {
        echo "cd /t"
        echo 'echo "== vars"'
        echo "/mk21 -s -f vars.mk"
        echo 'echo "== rules"'
        echo "/mk21 -s -f rules.mk"
        echo 'echo "== uptodate"'
        echo "/mk21 -s -f uptodate.mk"
        echo 'echo "== uptodate2"'
        echo "/mk21 -f uptodate.mk"
        echo 'echo "== end"'
    } > "$work/root/go.sh"
    OS18_KERNEL=kernel30.bin sh tools/os18.sh run "$work/root" "$work/out"
    for n in vars rules uptodate uptodate2; do
        sed -n "/^== $n\$/,/^== /p" "$work/out.console" | sed '1d;$d' > "$work/$n.out"
    done
}

cmd=${1:-}
case "$cmd" in
host) build_host ;;
stone)
    [ $# -eq 2 ] || die "stone <作業の置き場>"
    stone "$2"
    ;;
*)
    echo "usage: mk21.sh {host | stone <作業の置き場>}" >&2
    exit 2
    ;;
esac
