#!/bin/sh
# Linux 5.4 の Kbuild を我々の道具で回す (docs/stage018-ncc.md 10 章の M5)。
#
#   kb18.sh run <出力> <設定の目標> [make の変数=値...]
#       O=<出力> に設定を作り，vmlinux と arch/riscv/boot/Image まで組む。道具は CROSS_COMPILE で
#       我々のものに差し替える。KB18_NPP を与えると前処理を npp1 で行う (tools/kb18cc.sh)
#
#   kb18.sh cmp <出力 1> <出力 2>
#       2 つの出力の生成物と依存の記録を比べる
#
# 道具の対応:
#   gcc                      tools/kb18cc.sh (ホストの cpp と ncc1・nas1・nld1)
#   ld                       nld1
#   ar                       nar1
#   nm / objcopy / objdump   nobj1
#
# 道具はホストの cc で組んだもの (tools/ncc18.sh host が tmp/nas1/host に
# 置く) を使う。Kbuild が組んで走らせるホストの補助プログラム (fixdep など)
# は HOSTCC (ホストの gcc) のままである。
#
# 設定は tools/kbuildlog.sh と同じく上流の conf で作る (flex が要る)。
# 前回の生成物があれば make はそれを使う (途中から続けられる)。初めから
# 組み直すには <出力> を消す。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
linux="$repo_root/docs/external/linux54"
flexbin="$repo_root/tmp/kconfref/flex/src/flex"
host="$repo_root/tmp/nas1/host"

die() { echo "kb18.sh: $*" >&2; exit 1; }

# 道具の入口を <dir> に作る
make_tools() {
    d=$1
    mkdir -p "$d"
    for t in ncc1 nas1 nld1 nar1 nobj1; do
        [ -x "$host/$t" ] || die "$t が無い (sh tools/ncc18.sh host)"
    done
    printf '#!/bin/sh\nexec sh "%s" "$@"\n' "$repo_root/tools/kb18cc.sh" > "$d/riscv32-stone-gcc"
    printf '#!/bin/sh\nexec "%s" "$@"\n' "$host/nld1" > "$d/riscv32-stone-ld"
    printf '#!/bin/sh\nexec "%s" "$@"\n' "$host/nar1" > "$d/riscv32-stone-ar"
    for t in nm objcopy objdump; do
        printf '#!/bin/sh\nexec "%s" %s "$@"\n' "$host/nobj1" "$t" > "$d/riscv32-stone-$t"
    done
    chmod +x "$d"/riscv32-stone-*
}

run_build() {
    out=$1; target=$2; shift 2
    [ -x "$flexbin" ] || die "flex が無い (sh tools/kconfref.sh flex)"
    mkdir -p "$out"
    out=$(CDPATH='' cd -- "$out" && pwd)
    make_tools "$out/kb18/bin"
    KB18_ROOT=$repo_root
    KB18_BIN=$host
    export KB18_ROOT KB18_BIN
    # 版の文字列 (init/version.o) と initramfs の時刻を固定し，同じ入力から
    # 同じ vmlinux を作る (2 つの出力を cmp で比べられるように)
    KBUILD_BUILD_TIMESTAMP=${KBUILD_BUILD_TIMESTAMP:-Thu Jan  1 00:00:00 UTC 1970}
    KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-stone}
    KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-stone}
    export KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_USER KBUILD_BUILD_HOST
    set -- O="$out" ARCH=riscv CROSS_COMPILE="$out/kb18/bin/riscv32-stone-" \
        LEX="$flexbin" YACC=bison "$@"
    if [ ! -f "$out/.config" ]; then
        make -C "$linux" "$@" "$target" > "$out/kb18/config.out" 2>&1 \
            || die "設定の目標が失敗した ($out/kb18/config.out)"
    fi
    # Image は arch/riscv/boot の objcopy -O binary (起動に使う。docs/stage018-ncc.md 17 章)
    if make -k -C "$linux" "$@" V=1 vmlinux Image > "$out/kb18/build.out" 2>&1; then
        echo "kb18.sh: vmlinux と Image ができた ($out/vmlinux，$out/arch/riscv/boot/Image)"
    else
        echo "kb18.sh: vmlinux の目標が失敗した ($out/kb18/build.out)" >&2
        return 1
    fi
}

# 2 つの出力を比べる (前処理器を差し替えた時の確かめ。docs/stage019-linux.md 8 章)。
# .o・.a・.so・vmlinux・Image・設定・依存の記録 (.cmd) は一致を求める。.cmd の
# 中の出力先の経路は置き換えてから比べる。リンカ台本は空行と行の中の空白の量を
# 除いて比べる。組んだ時刻は run が固定するので，違いは全て説明の無いものとする
compare() {
    a=$(CDPATH='' cd -- "$1" && pwd)
    b=$(CDPATH='' cd -- "$2" && pwd)
    n=0; bad=0
    list=$(cd "$a" && find . -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' \
        -o -name '*.lds' -o -name '.*.cmd' -o -name '.config' -o -name 'System.map' \
        -o -name 'vmlinux' -o -name 'Image' \) -not -path './kb18/*' | sort)
    for f in $list; do
        n=$((n + 1))
        if [ ! -f "$b/$f" ]; then
            echo "無い: $f"
            bad=$((bad + 1))
            continue
        fi
        case "$f" in
        *.lds) [ "$(squeeze < "$a/$f")" = "$(squeeze < "$b/$f")" ] && continue ;;
        *.cmd) [ "$(sed "s|$a|@|g" "$a/$f")" = "$(sed "s|$b|@|g" "$b/$f")" ] && continue ;;
        *) cmp -s "$a/$f" "$b/$f" && continue ;;
        esac
        echo "違う: $f"
        bad=$((bad + 1))
    done
    echo "kb18.sh: $n 個を比べ，違いが $bad 個"
    [ "$bad" -eq 0 ]
}

squeeze() {
    tr -s ' \t' '  ' | sed 's/^ //; s/ $//; /^$/d'
}

cmd=${1:-}
case "$cmd" in
run)
    [ $# -ge 3 ] || die "run <出力> <設定の目標> [変数=値...]"
    shift
    run_build "$@"
    ;;
cmp)
    [ $# -eq 3 ] || die "cmp <出力 1> <出力 2>"
    compare "$2" "$3"
    ;;
*)
    echo "usage: kb18.sh {run <出力> <設定の目標> [変数=値...] | cmp <出力 1> <出力 2>}" >&2
    exit 2
    ;;
esac
