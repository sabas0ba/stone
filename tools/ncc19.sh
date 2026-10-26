#!/bin/sh
# stone の OS の上でドライバ ncc に Kbuild の gcc の命令行をそのまま走らせ，
# ホストで組んだ ncc の出力とバイト単位で比べる (docs/stage019-linux.md 9 章)。
#
#   ncc19.sh host
#       ホスト版の ncc を tmp/npp1/ncc に組む (npp1 も tmp/npp1/npp1 に組む)
#   ncc19.sh stone <objtree> <作業の置き場> <出力>...
#       <objtree> は KB18_NCC を与えて tools/kb18.sh run で組んだもの。<出力> は
#       Kbuild の出力の名前 (mm/highmem.o など)
#
# 手順:
#   1. <objtree> の記録 (.<名前>.cmd) から <出力> を作った命令を取り出す。
#      && や ; で後ろにつながる命令 (objcopy など) は除き，gcc の部分だけを使う
#   2. その命令をホスト版の ncc で <objtree> の中で走らせ，出力と依存の記録
#      (-Wp,-MMD,<file>) を <作業>/ref に写す
#   3. 命令が名指すファイルと依存の記録にあるファイル，それに処理系の資料
#      (stage019/include) を，ホストと同じ絶対経路で <作業>/root の下に写す。
#      stone の上でも同じ場所で同じ命令行を走らせられる
#   4. ncc・npp1・ncc1・nas1・nld1 (stone で組んだもの) と処理系の前定義を
#      /lib/ncc に置き，go.sh から命令の頭の gcc を /lib/ncc/ncc に替えて
#      走らせる。カーネルは引数の上限を広げた kernel30
#   5. 出力と依存の記録を ref と比べる。依存の記録は処理系の前定義の経路
#      (ホストは stage019/predef-rv32-gcc46.h，stone は /lib/ncc/predef.h) を
#      揃えてから比べる
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
ncc_host="$repo_root/tmp/npp1/ncc"
npp_host="$repo_root/tmp/npp1/npp1"
predef="$repo_root/stage019/predef-rv32-gcc46.h"
ccinc="$repo_root/stage019/include"
HOSTCC=${HOSTCC:-gcc}

die() { echo "ncc19.sh: $*" >&2; exit 1; }

build_host() {
    mkdir -p tmp/npp1
    "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
        -o "$npp_host" stage019/npp1.c
    "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
        -D_POSIX_C_SOURCE=200112L -DNCC_HOST -o "$ncc_host" stage019/ncc.c
    echo "built $npp_host $ncc_host" >&2
}

# <出力> を作った命令 (gcc の部分) を出す。頭の語は gcc の入口の経路
command_of() {
    kb=$1; o=$2
    d=$(dirname "$o"); b=$(basename "$o")
    cmdf="$kb/$d/.$b.cmd"
    [ -f "$cmdf" ] || die "記録が無い: $cmdf"
    sed -n "s|^cmd_$o := ||p" "$cmdf" | sed 's/ && .*//; s/; .*//'
}

# 依存の記録 (target: a b \ ...) からファイルの名前を 1 行 1 個で出す
dep_files() {
    sed 's/\\$//' "$1" | tr ' ' '\n' | sed '/^$/d; /:$/d'
}

# 命令の -o の名前を出す (結合では記録の名前と違うことがある: vdso.so.dbg は
# gcc が vdso.so.dbg.tmp を作り，objcopy がそれを写す)
out_of() {
    printf '%s\n' "$1" | tr ' ' '\n' | sed -n '/^-o$/{n;p;}' | head -n 1
}

# 命令の -Wp,-MMD,<file> の <file> を出す (無ければ空)
dep_of() {
    printf '%s\n' "$1" | tr ' ' '\n' | sed -n 's/^-Wp,-MM\{0,1\}D,//p' | head -n 1
}

stone() {
    kb=$(CDPATH='' cd -- "$1" && pwd); work=$2; shift 2
    [ -x "$ncc_host" ] && [ -x "$npp_host" ] || die "ホスト版が無い (sh tools/ncc19.sh host)"
    for t in ncc npp1 ncc1 nas1 nld1 kernel30.bin; do
        [ -s "tmp/build/$t" ] || die "stone の生成物が無い: tmp/build/$t (sh tools/build.sh stage019)"
    done
    work=$(mkdir -p "$work" && CDPATH='' cd -- "$work" && pwd)
    rm -rf "$work/ref" "$work/root" "$work/out" "$work/out.console"
    mkdir -p "$work/ref" "$work/root/lib/ncc"
    for t in ncc npp1 ncc1 nas1 nld1; do
        cp "tmp/build/$t" "$work/root/lib/ncc/$t"
    done
    cp "$predef" "$work/root/lib/ncc/predef.h"
    mkdir -p "$work/root$ccinc"
    cp -R "$ccinc"/. "$work/root$ccinc/"
    mkdir -p "$work/root$kb"
    printf "cd '%s'\n" "$kb" > "$work/root/go.sh"
    for o in "$@"; do
        key=$(printf '%s' "$o" | tr '/' '_')
        c=$(command_of "$kb" "$o")
        [ -n "$c" ] || die "命令が取れない: $o"
        gcc_path=${c%% *}
        args=${c#* }
        dep=$(dep_of "$c")
        target=$(out_of "$c")
        [ -n "$target" ] || die "命令に -o が無い: $o"
        # 2. ホストでの答え。命令は .cmd の中の sh の書き方のまま走らせる
        (
            cd "$kb"
            NCC_BIN="$kb/kb18/bin/ncc-bin"
            NCC_PREDEF="$predef"
            NCC_INCLUDE="$ccinc"
            export NCC_BIN NCC_PREDEF NCC_INCLUDE
            eval "\"\$ncc_host\" $args"
        ) || die "ホストの ncc が失敗した: $o"
        cp "$kb/$target" "$work/ref/$key"
        if [ -n "$dep" ]; then
            cp "$kb/$dep" "$work/ref/$key.d"
        fi
        # 3. 命令が名指すファイルと依存の記録のファイル
        {
            # -Wl,a,b の中の名前 (-Wl,-T,<台本>) も拾う
            for w in $args; do
                case "$w" in
                -Wl,*) printf '%s\n' "${w#-Wl,}" | tr ',' '\n' ;;
                *) printf '%s\n' "$w" ;;
                esac
            done
            [ -z "$dep" ] || dep_files "$work/ref/$key.d"
        } | while IFS= read -r f; do
            case "$f" in
            /*) a=$f ;;
            *) a=$kb/$f ;;
            esac
            [ -f "$a" ] || continue
            [ "$a" = "$kb/$target" ] && continue
            mkdir -p "$(dirname "$work/root$a")"
            cp "$a" "$work/root$a"
        done
        mkdir -p "$(dirname "$work/root$kb/$target")"
        [ -z "$dep" ] || mkdir -p "$(dirname "$work/root$kb/$dep")"
        # 4. go.sh に足す。命令の頭の gcc を stone の ncc に替える
        [ "$gcc_path" != "${gcc_path%riscv32-stone-gcc}" ] || die "gcc の命令でない: $o"
        printf '/lib/ncc/ncc %s\n' "$args" >> "$work/root/go.sh"
        printf 'echo "rc %s $?"\n' "$key" >> "$work/root/go.sh"
    done
    nent=$(find "$work/root" | wc -l)
    echo "ncc19.sh: 根の項目 $nent 個，$(du -sk "$work/root" | cut -f 1) KiB" >&2
    OS18_KERNEL=kernel30.bin OS18_MAXENT=$((nent + 3 * $# + 2 + 64)) \
        sh tools/os18.sh run "$work/root" "$work/out"
    # 5. 比べる
    for o in "$@"; do
        key=$(printf '%s' "$o" | tr '/' '_')
        c=$(command_of "$kb" "$o")
        dep=$(dep_of "$c")
        target=$(out_of "$c")
        rc=$(sed -n "s/^rc $key //p" "$work/out.console")
        if [ "$rc" != 0 ]; then
            res="rc=${rc:-無し}"
        elif ! cmp -s "$work/out$kb/$target" "$work/ref/$key"; then
            res="diff"
        elif [ -n "$dep" ] && [ "$(sed "s|/lib/ncc/predef.h|$predef|" "$work/out$kb/$dep")" != "$(cat "$work/ref/$key.d")" ]; then
            res="ddiff"
        else
            res="same"
        fi
        printf '%s\t%s\n' "$o" "$res"
    done
}

cmd=${1:-}
case "$cmd" in
host)
    build_host
    ;;
stone)
    [ $# -ge 4 ] || die "stone <objtree> <作業の置き場> <出力>..."
    shift
    stone "$@"
    ;;
*)
    echo "usage: ncc19.sh {host | stone <objtree> <作業の置き場> <出力>...}" >&2
    exit 2
    ;;
esac
