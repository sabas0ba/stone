#!/bin/sh
# 用意した根で stone の OS (kernel29) を 1 回起動し，go.sh を走らせる
# (docs/stage018-ncc.md 7 章)。
#
#   os18.sh run <根> <取り出し先>
#
# <根> には走らせるプログラムと入力，それに go.sh (sh2 の台本) を置いておく。
# ここで sh2 と boot を足して sfs4 に詰め，kernel29 を起動し，走らせた後の
# 根を <取り出し先> へ展開する。端末の出力は <取り出し先>.console に置く。
#
# kernel29 は RAM 2 GiB を使い，sfs の窓は RAM ファイルの 0x6000_0000 から
# 先にある (tools/gcc17.sh の run_cc1 と同じ)。イメージは 64 MiB 取る。
# 項目の数の上限は既定で 1024。多い根は OS18_MAXENT で広げる。
# カーネルは OS18_KERNEL (tmp/build の下の名前。既定 kernel29.bin) で替えられる
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
run_ram=2147483648
run_win=1610612736
imgsize=67108864

die() { echo "os18.sh: $*" >&2; exit 1; }

run() {
    root=$1
    out=$2
    [ -f "$root/go.sh" ] || die "go.sh が無い: $root"
    kernel=${OS18_KERNEL:-kernel29.bin}
    for f in "$kernel" sh2.bin; do
        [ -s "tmp/build/$f" ] || die "OS の生成物が無い: tmp/build/$f (sh tools/build.sh stage017)"
    done
    cp tmp/build/sh2.bin "$root/sh2"
    printf 'sh2 go.sh\n' > "$root/boot"
    w="$out.work"
    rm -rf "$w" "$out"
    mkdir -p "$w"
    sh tools/sfs4.sh pack "$root" "$w/run.img" "$imgsize" "${OS18_MAXENT:-1024}" > /dev/null \
        || die "sfs4 に詰められない"
    dd if=/dev/null of="$w/run.ram" bs=1 seek="$run_ram" 2> /dev/null
    dd if="$w/run.img" of="$w/run.ram" bs=64K oflag=seek_bytes \
        seek="$run_win" conv=notrunc 2> /dev/null
    # RAM ファイルはリポジトリからの相対で渡す (コンテナの中の QEMU は
    # ホストの絶対経路を見られない)
    case "$w" in
    /*) rel=${w#"$repo_root"/} ;;
    *) rel=$w ;;
    esac
    STONE_QEMU_RAMFILE="$rel/run.ram" STONE_QEMU_RAM=2G \
        sh tools/env.sh qemu "tmp/build/$kernel" < /dev/null > "$out.console" 2>&1 || true
    dd if="$w/run.ram" of="$w/out.img" bs=64K iflag=skip_bytes,count_bytes \
        skip="$run_win" count="$imgsize" 2> /dev/null
    sh tools/sfs4.sh unpack "$w/out.img" "$out" > /dev/null \
        || die "走らせた後の根を取り出せない"
    rm -rf "$w"
}

case "${1:-}" in
run)
    [ $# -eq 3 ] || die "run <根> <取り出し先>"
    run "$2" "$3"
    ;;
*)
    echo "usage: os18.sh run <根> <取り出し先>" >&2
    exit 2
    ;;
esac
