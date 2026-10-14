#!/bin/sh
# nas1 (stage018/nas1.c) を組み，走らせ，突き合わせる (docs/stage018-ncc.md 10 章の M1)。
#
#   nas18.sh host                    ホストの cc で組む (tmp/nas1/host/nas1)
#   nas18.sh run <出力dir> <入力.s>...  stone 版を kernel29 の上で走らせ，.o を <出力dir> へ
#                                    (既定はビルドの生成物 tmp/build/nas1。NAS18_BIN で替える)
#   nas18.sh cmp <入力.s>...           ホスト版と stone 版の .o をバイト単位で突き合わせる
#
# ホスト版は開発中の測定に使う (docs/stage019-linux.md 2 章)。stone 版は
# 完了条件の側の実体で，sh tools/build.sh stage018 が tmp/build/nas1 に組む
# (tools/build/stage018.sh)。2 つは同じ入力から同じ .o を出さなければならない
# (docs/stage018-ncc.md 7 章の 5)。
#
# run は入力を 1 つの根に並べ，1 回の起動でまとめて組み立てる。入力の名前は
# 根の中で i<n>.s に付け替える。.incbin が読むファイルは NAS18_INC
# (ディレクトリ) の中身を根へ写して渡す。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
work="$repo_root/tmp/nas1"
src=stage018/nas1.c
HOSTCC=${HOSTCC:-gcc}

die() { echo "nas18.sh: $*" >&2; exit 1; }

build_host() {
    mkdir -p "$work/host"
    "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
        -o "$work/host/nas1" "$src"
    echo "built $work/host/nas1" >&2
}

# kernel29 は RAM 2 GiB を使い，sfs の窓は RAM ファイルの 0x6000_0000 から先
# にある (tools/gcc17.sh の run_cc1 と同じ)
run_ram=2147483648
run_win=1610612736

run_stone() {
    o=$1
    shift
    bin=${NAS18_BIN:-tmp/build/nas1}
    [ -s "$bin" ] || die "stone 版が無い: $bin (sh tools/build.sh stage018)"
    for f in kernel29.bin sh2.bin; do
        [ -s "tmp/build/$f" ] || die "OS の生成物が無い: tmp/build/$f"
    done
    r="$work/run"
    rm -rf "$r"
    mkdir -p "$r/root" "$o"
    cp "$bin" "$r/root/nas1"
    cp tmp/build/sh2.bin "$r/root/sh2"
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
    printf 'sh2 go.sh\n' > "$r/root/boot"
    sh tools/sfs4.sh pack "$r/root" "$r/run.img" 67108864 1024 > /dev/null \
        || die "sfs4 に詰められない"
    dd if=/dev/null of="$r/run.ram" bs=1 seek="$run_ram" 2> /dev/null
    dd if="$r/run.img" of="$r/run.ram" bs=64K oflag=seek_bytes \
        seek="$run_win" conv=notrunc 2> /dev/null
    # RAM ファイルはリポジトリからの相対で渡す (コンテナの中の QEMU は
    # ホストの絶対経路を見られない)
    STONE_QEMU_RAMFILE="${r#"$repo_root"/}/run.ram" STONE_QEMU_RAM=2G \
        sh tools/env.sh qemu tmp/build/kernel29.bin < /dev/null > "$r/console.log" 2>&1 || true
    dd if="$r/run.ram" of="$r/out.img" bs=64K iflag=skip_bytes,count_bytes \
        skip="$run_win" count=67108864 2> /dev/null
    sh tools/sfs4.sh unpack "$r/out.img" "$r/out" > /dev/null \
        || die "走らせた後の根を取り出せない"
    rm -f "$r/run.ram" "$r/out.img"
    while IFS="$(printf '\t')" read -r i f; do
        b=$(basename "$f" .s)
        rc=$(sed -n "s/^rc $i \\([0-9][0-9]*\\)\$/\\1/p" "$r/console.log" | tail -n 1)
        if [ "$rc" = 0 ] && [ -f "$r/out/i$i.o" ]; then
            cp "$r/out/i$i.o" "$o/$b.o"
        else
            rm -f "$o/$b.o"
            echo "stone 版が失敗した: $f (rc ${rc:-なし}，$r/console.log)" >&2
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
