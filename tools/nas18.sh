#!/bin/sh
# nas1 (stage018/nas1.c) を組み，走らせ，突き合わせる (docs/stage018-ncc.md 10 章の M1)。
#
#   nas18.sh host                    nas1 と nld1 をホストの cc で組む (tmp/nas1/host/)
#   nas18.sh run <出力dir> <入力.s>...  stone 版を kernel29 の上で走らせ，.o を <出力dir> へ
#                                    (既定はビルドの生成物 tmp/build/nas1。NAS18_BIN で替える)
#   nas18.sh cmp <入力.s>...           ホスト版と stone 版の .o をバイト単位で突き合わせる
#
# ホスト版は開発中の測定に使う (docs/stage019-linux.md 2 章)。stone 版は
# 完了条件の側の実体で，sh tools/build.sh stage018 が tmp/build/nas1 に組む
# (tools/build/stage018.sh)。2 つは同じ入力から同じ .o を出さなければならない
# (docs/stage018-ncc.md 7 章の 5)。
#
# run は入力を 1 つの根に並べ，1 回の起動でまとめて組み立てる (tools/os18.sh)。入力の名前は
# 根の中で i<n>.s に付け替える。.incbin が読むファイルは NAS18_INC
# (ディレクトリ) の中身を根へ写して渡す。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
work="$repo_root/tmp/nas1"
HOSTCC=${HOSTCC:-gcc}

die() { echo "nas18.sh: $*" >&2; exit 1; }

# nas1 と nld1 をホストの cc で組む
build_host() {
    mkdir -p "$work/host"
    for p in nas1 nld1; do
        "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
            -o "$work/host/$p" "stage018/$p.c"
        echo "built $work/host/$p" >&2
    done
}

run_stone() {
    o=$1
    shift
    bin=${NAS18_BIN:-tmp/build/nas1}
    [ -s "$bin" ] || die "stone 版が無い: $bin (sh tools/build.sh stage018)"
    r="$work/run"
    rm -rf "$r"
    mkdir -p "$r/root" "$o"
    cp "$bin" "$r/root/nas1"
    if [ -n "${NAS18_INC:-}" ]; then
        cp -R "$NAS18_INC/." "$r/root/"
    fi
    : > "$r/root/go.sh"
    : > "$r/names"
    n=0
    for f in "$@"; do
        n=$((n + 1))
        cp "$f" "$r/root/i$n.s"
        printf 'nas1 -o i%s.o i%s.s\necho "rc %s $?"\n' "$n" "$n" "$n" >> "$r/root/go.sh"
        printf '%s\t%s\n' "$n" "$f" >> "$r/names"
    done
    sh tools/os18.sh run "$r/root" "$r/out"
    while IFS="$(printf '\t')" read -r i f; do
        b=$(basename "$f" .s)
        rc=$(sed -n "s/^rc $i \\([0-9][0-9]*\\)\$/\\1/p" "$r/out.console" | tail -n 1)
        if [ "$rc" = 0 ] && [ -f "$r/out/i$i.o" ]; then
            cp "$r/out/i$i.o" "$o/$b.o"
        else
            rm -f "$o/$b.o"
            echo "stone 版が失敗した: $f (rc ${rc:-なし}，$r/out.console)" >&2
        fi
    done < "$r/names"
}

cmp_both() {
    [ -x "$work/host/nas1" ] || die "ホスト版が無い (sh tools/nas18.sh host)"
    rm -rf "$work/cmp"
    mkdir -p "$work/cmp/host" "$work/cmp/stone"
    for f in "$@"; do
        b=$(basename "$f" .s)
        if [ -n "${NAS18_INC:-}" ]; then
            "$work/host/nas1" -I "$NAS18_INC" -o "$work/cmp/host/$b.o" "$f"
        else
            "$work/host/nas1" -o "$work/cmp/host/$b.o" "$f"
        fi
    done
    run_stone "$work/cmp/stone" "$@"
    rc=0
    for f in "$@"; do
        b=$(basename "$f" .s)
        if cmp -s "$work/cmp/host/$b.o" "$work/cmp/stone/$b.o"; then
            echo "same    $b"
        else
            echo "differ  $b"
            rc=1
        fi
    done
    return $rc
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
host) build_host ;;
run)
    [ $# -ge 2 ] || die "run <出力dir> <入力.s>..."
    run_stone "$@"
    ;;
cmp)
    [ $# -ge 1 ] || die "cmp <入力.s>..."
    cmp_both "$@"
    ;;
*)
    echo "usage: nas18.sh {host | run <出力dir> <入力.s>... | cmp <入力.s>...}" >&2
    exit 2
    ;;
esac
