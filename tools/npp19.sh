#!/bin/sh
# stone の OS の上で npp1 に Linux の前処理をさせ，ホストの cc で組んだ npp1 の
# 出力とバイト単位で比べる (docs/stage019-linux.md 8 章)。
#
#   npp19.sh stone <命令行の置き場> <作業の置き場> <出力>...
#
# <命令行の置き場> は tools/kb18cc.sh が KB18_NPP_LOG に残したもの (出力ごとに
# 1 ファイル。1 行目が作業の場所，続く行が npp1 の選択肢)。<出力> は Kbuild の
# 出力の名前 (mm/highmem.o など) で与える。
#
# 手順:
#   1. ホストの npp1 を同じ選択肢で走らせ，.i と依存 (.d) を <作業>/ref に置く。
#      もう 1 度 -MD で走らせて，読むファイルを全て数える (処理系の階層も含む)
#   2. 読むファイルをホストと同じ絶対経路で <作業>/root の下に写す。stone の上
#      でも同じ場所で同じ命令行を走らせられるので，__FILE__ や依存の経路も
#      同じになる
#   3. go.sh を作り，tools/os18.sh で stone の OS の上で走らせる
#   4. 取り出した .i と .d を ref と比べ，出力ごとに same / diff / rc を出す
#
# 出力の .i と .d は根の /out に書く。命令行の -MMD / -MD の置き場はそこへ
# 差し替え，-o を足す。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
npp_host="$repo_root/tmp/npp1/npp1"

die() { echo "npp19.sh: $*" >&2; exit 1; }

# 記録した選択肢 (2 行目から) の依存の置き場を $2 に替えて 1 行 1 個で出す
args_with_dep() {
    tail -n +2 "$1" | awk -v d="$2" 'r { print d; r = 0; next }
                                     $0 == "-MMD" || $0 == "-MD" || $0 == "-MF" { print; r = 1; next }
                                     { print }'
}

# 依存の記録 (target: a b \ ...) からファイルの名前を 1 行 1 個で出す
dep_files() {
    sed 's/\\$//' "$1" | tr ' ' '\n' | sed '/^$/d; /:$/d'
}

stone() {
    logs=$1; work=$2; shift 2
    [ -x "$npp_host" ] || die "ホストの npp1 が無い ($npp_host)"
    [ -s tmp/build/npp1 ] || die "stone の npp1 が無い (sh tools/build.sh stage019)"
    work=$(mkdir -p "$work" && CDPATH='' cd -- "$work" && pwd)
    logs=$(CDPATH='' cd -- "$logs" && pwd)
    rm -rf "$work/ref" "$work/all" "$work/root" "$work/out" "$work/out.console"
    mkdir -p "$work/ref" "$work/all" "$work/root/out"
    cp tmp/build/npp1 "$work/root/npp1"
    : > "$work/root/go.sh"
    for o in "$@"; do
        key=$(printf '%s' "$o" | tr '/' '_')
        log="$logs/$key"
        [ -s "$log" ] || die "命令行が無い: $log"
        cwd=$(head -n 1 "$log")
        # 1. ホストでの答えと，読むファイルの一覧
        (
            cd "$cwd"
            IFS='
'
            set -f
            # shellcheck disable=SC2046
            "$npp_host" $(args_with_dep "$log" "$work/ref/$key.d") -o "$work/ref/$key.i"
            # shellcheck disable=SC2046
            "$npp_host" $(args_with_dep "$log" "$work/all/$key.d") -MD "$work/all/$key.d" \
                -o /dev/null
        ) || die "ホストの npp1 が失敗した: $o"
        # 2. 読むファイルを同じ絶対経路へ写す
        mkdir -p "$work/root$cwd"
        dep_files "$work/all/$key.d" | while IFS= read -r f; do
            case "$f" in
            /*) a=$f ;;
            *) a=$cwd/$f ;;
            esac
            mkdir -p "$(dirname "$work/root$a")"
            cp "$a" "$work/root$a"
        done
        # 3. go.sh に 1 行足す。選択肢は ' で囲む (' を含むものは扱わない)
        if grep -q "'" "$log"; then
            die "選択肢に ' がある: $log"
        fi
        {
            printf "cd '%s'\n" "$cwd"
            printf '/npp1'
            args_with_dep "$log" "/out/$key.d" | while IFS= read -r a; do
                printf " '%s'" "$a"
            done
            printf " -o '/out/%s.i'\n" "$key"
            printf 'echo "rc %s $?"\n' "$key"
        } >> "$work/root/go.sh"
    done
    nent=$(find "$work/root" | wc -l)
    echo "npp19.sh: 根の項目 $nent 個，$(du -sk "$work/root" | cut -f 1) KiB" >&2
    OS18_MAXENT=$((nent + 256)) sh tools/os18.sh run "$work/root" "$work/out"
    # 4. 比べる
    for o in "$@"; do
        key=$(printf '%s' "$o" | tr '/' '_')
        rc=$(sed -n "s/^rc $key //p" "$work/out.console")
        if [ "$rc" != 0 ]; then
            res="rc=${rc:-無し}"
        elif cmp -s "$work/out/out/$key.i" "$work/ref/$key.i" \
            && cmp -s "$work/out/out/$key.d" "$work/ref/$key.d"; then
            res=same
        else
            res="diff"
        fi
        printf '%s\t%s\n' "$o" "$res"
    done
}

cmd=${1:-}
case "$cmd" in
stone)
    [ $# -ge 4 ] || die "stone <命令行の置き場> <作業の置き場> <出力>..."
    shift
    stone "$@"
    ;;
*)
    echo "usage: npp19.sh stone <命令行の置き場> <作業の置き場> <出力>..." >&2
    exit 2
    ;;
esac
