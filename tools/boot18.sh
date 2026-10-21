#!/bin/sh
# 我々の SBI の上で，我々の道具で組んだ Linux を QEMU virt (rv32) で起動し，
# initramfs の init まで走らせる (docs/stage018-ncc.md 10 章の M6・17 章)。
#
#   boot18.sh build <kbout>     SBI (sbi1)・init (init1)・initramfs を組む
#   boot18.sh run <kbout>       起動する。console は tmp/boot18/console.log
#
# <kbout> は tools/kb18.sh run の出力先 (arch/riscv/boot/Image と，Kbuild が
# ホストの cc で組んだ usr/gen_init_cpio を使う)。
#
# SBI と init はホストの cc で組んだ ncc1・nas1・nld1 (tools/ncc18.sh host) で
# 組む。前処理はホストの cpp で行う (8 章)。
#
# 起動の形: -bios に SBI の ELF を，-kernel に Image を，-initrd に cpio を
# 渡す。QEMU は Image を 0x8040_0000 に置き，その番地を fw_dynamic_info で
# SBI に知らせる。init は syscall を確かめて reboot(POWER_OFF) で電源を切り，
# SBI の shutdown が test finisher に書いて QEMU が終了コード 0 で止まる。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
# コンテナの中の QEMU にはリポジトリからの相対経路で渡す
work=tmp/boot18
host=tmp/nas1/host
HOSTCC=${HOSTCC:-gcc}

die() { echo "boot18.sh: $*" >&2; exit 1; }

# ホストの cpp で前処理して ncc1 で訳し，nas1 で組み立てる
cc() {
    src=$1; obj=$2
    "$HOSTCC" -E -P -undef -nostdinc -x c "$src" -o "$obj.i"
    "$host/ncc1" -o "$obj.s" "$obj.i"
    "$host/nas1" -o "$obj" "$obj.s"
}

build() {
    kb=$1
    for t in ncc1 nas1 nld1; do
        [ -x "$host/$t" ] || die "$t が無い (sh tools/ncc18.sh host)"
    done
    [ -x "$kb/usr/gen_init_cpio" ] || die "gen_init_cpio が無い (sh tools/kb18.sh run)"
    mkdir -p "$work"
    # SBI: nld1 の既定の台本 (0x8000_0000 から，.text.init が先頭)
    "$host/nas1" -o "$work/sbi1s.o" stage018/sbi1s.s
    cc stage018/sbi1.c "$work/sbi1.o"
    "$host/nld1" -o "$work/sbi1.elf" "$work/sbi1s.o" "$work/sbi1.o"
    # init: 利用者の空間 (init1.lds)
    "$host/nas1" -o "$work/init1s.o" stage018/init1s.s
    cc stage018/init1.c "$work/init1.o"
    "$host/nld1" -T stage018/init1.lds -o "$work/init" "$work/init1s.o" "$work/init1.o"
    # initramfs: Kbuild の gen_init_cpio で作る
    {
        echo "dir /dev 0755 0 0"
        echo "nod /dev/console 0600 0 0 c 5 1"
        echo "file /init $repo_root/$work/init 0755 0 0"
    } > "$work/initramfs.list"
    "$kb/usr/gen_init_cpio" "$work/initramfs.list" > "$work/initramfs.cpio"
    echo "boot18.sh: $work/sbi1.elf，$work/init，$work/initramfs.cpio を組んだ" >&2
}

run() {
    kb=$1
    img="$kb/arch/riscv/boot/Image"
    [ -s "$img" ] || die "Image が無い (sh tools/kb18.sh run)"
    [ -s "$work/sbi1.elf" ] || die "SBI が無い (sh tools/boot18.sh build)"
    case "$img" in
    /*) img=${img#"$repo_root"/} ;;
    esac
    set +e
    sh tools/env.sh run timeout 300 qemu-system-riscv32 -M virt -m 128M -smp 1 \
        -display none -monitor none -serial stdio -no-reboot \
        -bios "$work/sbi1.elf" -kernel "$img" -initrd "$work/initramfs.cpio" \
        -append "earlycon=sbi console=hvc0 rdinit=/init" \
        < /dev/null > "$work/console.log" 2>&1
    rc=$?
    set -e
    echo "boot18.sh: QEMU の終了コード $rc (console: $work/console.log)" >&2
    return $rc
}

cmd=${1:-}
case "$cmd" in
build) [ $# -eq 2 ] || die "build <kbout>"; build "$2" ;;
run) [ $# -eq 2 ] || die "run <kbout>"; run "$2" ;;
*)
    echo "usage: boot18.sh {build <kbout> | run <kbout>}" >&2
    exit 2
    ;;
esac
