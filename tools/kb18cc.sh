#!/bin/sh
# Kbuild をホストから駆動する時の gcc の代わり (docs/stage018-ncc.md 3 章の ncc)。
#
# tools/kb18.sh が CROSS_COMPILE の gcc としてこの台本を置く。gcc の命令行を
# 読み，段ごとに我々の道具を呼ぶ。
#
#   -E                ホストの cpp (前処理だけ。8 章のとおり pp ができるまで)
#   -S  .c            cpp → ncc1
#   -c  .c            cpp → ncc1 → nas1
#   -c  .S            cpp (-D__ASSEMBLY__ は命令行が持つ) → nas1
#   それ以外          nld1 で結合する (-shared・-nostdlib・-Wl,…)
#
# cpp には -undef と処理系の定義 (stage019/predef-rv32-gcc46.h) を渡し，ホストの
# 定義を消す。処理系の include の階層 (-print-file-name=include) は
# stage019/include を答える。問合せ (--version・-dumpversion・-v) には
# GCC 4.6.4 として答える。cpp と ncc1 が知らない選択肢 (-W*・-f*・-m* など) は
# 読み捨てる。
#
# 道具は環境変数 KB18_BIN (ncc1・nas1・nld1・nobj1 の置き場) から，
# リポジトリの根は KB18_ROOT から取る。KB18_NPP に npp1 の経路があれば，
# ホストの cpp の代わりに npp1 で前処理する (docs/stage019-linux.md 8 章)。
# KB18_NPP_LOG (置き場) があれば npp1 の命令行を出力ごとに残す。
set -eu

root=${KB18_ROOT:?KB18_ROOT が無い}
bin=${KB18_BIN:?KB18_BIN が無い}
predef="$root/stage019/predef-rv32-gcc46.h"
ccinc="$root/stage019/include"
HOSTCC=${HOSTCC:-gcc}

die() { echo "kb18cc.sh: $*" >&2; exit 1; }

case " $* " in
*" --version "*) echo "riscv32-stone-gcc (GCC) 4.6.4"; exit 0 ;;
*" -dumpversion "*) echo "4.6.4"; exit 0 ;;
*" -print-file-name=include "*) echo "$ccinc"; exit 0 ;;
*" -print-file-name="*) echo "${*##*-print-file-name=}" | sed 's/ .*//'; exit 0 ;;
*" -v "*) echo "gcc version 4.6.4 (stone)" >&2; exit 0 ;;
esac

mode=link
out=""
lang=""
cmodel=""
inputs=""
# cpp と nld1 に渡す選択肢。1 行 1 個で持つ (空白を含む値を壊さない)
cppargs=$(mktemp)
ldargs=$(mktemp)
tmps="$cppargs $ldargs"
trap 'rm -f $tmps' EXIT

addc() { printf '%s\n' "$@" >> "$cppargs"; }
addl() { printf '%s\n' "$@" >> "$ldargs"; }

while [ $# -gt 0 ]; do
    a=$1
    case "$a" in
    -E|-c|-S) mode=$a ;;
    -o) out=$2; shift ;;
    -o*) out=${a#-o} ;;
    -x) lang=$2; shift ;;
    -I|-isystem|-include|-imacros|-D|-U|-idirafter|-iquote) addc "$a" "$2"; shift ;;
    -I*|-D*|-U*|-nostdinc|-P|-undef) addc "$a" ;;
    -Wp,*) addc "$a" ;;
    -MD|-MMD) addc "$a" ;;
    -MF|-MT|-MQ) addc "$a" "$2"; shift ;;
    -mcmodel=*) cmodel=${a#-mcmodel=} ;;
    -Wl,*)
        # -Wl,a,b,c は a b c を nld1 へ
        rest=${a#-Wl,}
        old_ifs=$IFS
        IFS=,
        # shellcheck disable=SC2086
        set -f
        for w in $rest; do addl "$w"; done
        set +f
        IFS=$old_ifs
        ;;
    -shared|-s|-r) addl "$a" ;;
    -T) addl -T "$2"; shift ;;
    -nostdlib|-nostartfiles|-no-pie|-static) ;;
    --param) shift ;;
    -) inputs="$inputs -" ;;
    -*) ;;
    *) inputs="$inputs $a" ;;
    esac
    shift
done

set -f
# shellcheck disable=SC2086
set -- $inputs
set +f

# cpp を走らせる: preproc <入力> <出力> <言語>
preproc() {
    src=$1; dst=$2; l=$3
    case "$l" in
    c) std='-std=gnu89'; xl=c ;;
    asm) std=''; xl=assembler-with-cpp ;;
    esac
    if [ "$cmodel" = medany ]; then
        addc -U__riscv_cmodel_medlow -D__riscv_cmodel_medany=1
    fi
    # 依存の印の目標は出力の名前にする (fixdep が読む)。依存を求められた時だけ
    # 付ける (gcc は -MD / -MMD の無い -MT を誤りとし，Kconfig の試しが落ちる)
    if [ -n "$out" ] && grep -q -e '^-MM\{0,1\}D$' -e '^-Wp,-MM\{0,1\}D,' "$cppargs"; then
        addc -MT "$out"
    fi
    if [ -n "${KB18_NPP:-}" ]; then
        # npp1 は cc1 の形の選択肢を読む: -Wp,-MMD,<f> は -MMD <f>。-std は持たない
        nargs=$(mktemp)
        tmps="$tmps $nargs"
        {
            printf '%s\n' -P -include "$predef"
            awk '/^-Wp,-MM?D,/ { i = index($0, ","); j = index(substr($0, i + 1), ",");
                                 print substr($0, i + 1, j - 1); print substr($0, i + j + 1); next }
                 { print }' "$cppargs"
            printf '%s\n' -x "$xl" "$src"
        } > "$nargs"
        # KB18_NPP_LOG があれば，出力ごとに作業の場所と選択肢 (-o を除く。1 行 1 個)
        # を残す (tests/stage019 が stone の上で同じ前処理を走らせる)
        if [ -n "${KB18_NPP_LOG:-}" ] && [ -n "$out" ]; then
            { pwd; cat "$nargs"; } > "$KB18_NPP_LOG/$(printf '%s' "$out" | tr '/' '_')"
        fi
    fi
    (
        IFS='
'
        set -f
        if [ -n "${KB18_NPP:-}" ]; then
            # shellcheck disable=SC2046
            "$KB18_NPP" $(cat "$nargs") -o "$dst"
        else
            # shellcheck disable=SC2046,SC2086
            "$HOSTCC" -E -P -undef $std -include "$predef" $(cat "$cppargs") -x "$xl" "$src" -o "$dst"
        fi
    )
}

langof() {
    case "$lang" in
    c) echo c; return ;;
    assembler-with-cpp) echo asm; return ;;
    esac
    case "$1" in
    *.S|*.lds.S) echo asm ;;
    *) echo c ;;
    esac
}

case "$mode" in
-E)
    [ $# -eq 1 ] || die "-E の入力は 1 つ: $*"
    l=$(langof "$1")
    if [ -z "$out" ]; then
        t=$(mktemp)
        tmps="$tmps $t"
        preproc "$1" "$t" "$l"
        cat "$t"
    else
        preproc "$1" "$out" "$l"
    fi
    ;;
-S|-c)
    [ $# -eq 1 ] || die "$mode の入力は 1 つ: $*"
    [ -n "$out" ] || die "$mode に -o が無い"
    src=$1
    l=$(langof "$src")
    # 中間の .i と .s は一時ファイルに置く。出力の隣に置くと，Kconfig の試し
    # (-o /dev/null) を並べて走らせた時に /dev/null.* を取り合う
    i=$(mktemp)
    s=$(mktemp)
    tmps="$tmps $i $s"
    preproc "$src" "$i" "$l"
    if [ "$l" = c ]; then
        ncc_opt=""
        [ "$cmodel" = medany ] && ncc_opt="-mcmodel=medany"
        if [ "$mode" = -S ]; then
            # shellcheck disable=SC2086
            "$bin/ncc1" $ncc_opt -o "$out" "$i"
            exit 0
        fi
        # shellcheck disable=SC2086
        "$bin/ncc1" $ncc_opt -o "$s" "$i"
        "$bin/nas1" -o "$out" "$s"
    else
        [ "$mode" = -c ] || die "-S は .c だけ: $src"
        "$bin/nas1" -o "$out" "$i"
    fi
    ;;
link)
    # 標準入力を組む試し (scripts/Kconfig.include の -m32 / -m64 など) は通す。
    # 設定は tools/kbuildlog.sh の記録と同じ答えにする
    if [ "${1:-}" = - ]; then
        cat > /dev/null
        exit 0
    fi
    [ -n "$out" ] || die "結合に -o が無い"
    (
        IFS='
'
        set -f
        # shellcheck disable=SC2046
        "$bin/nld1" $(cat "$ldargs") -o "$out" "$@"
    )
    ;;
esac
