#!/bin/sh
# sfs2 イメージとディレクトリの相互変換 (ホスト側の道具)。
# 形式は docs/stage016-os.md 6.3。
#
# 使用法:
#   sfs2.sh pack <dir> <img> [size] [maxent]   ディレクトリ -> イメージ
#   sfs2.sh unpack <img> <dir>                 イメージ -> ディレクトリ
#   sfs2.sh list <img>                         一覧 (種別・長さ・経路)
#
# 既定: size = 4194304 (4 MiB), maxent = 256。
#
# sfs1 (tools/sfs.sh) との違いは**ディレクトリを実体として持つ**こと。
# 名前はそのディレクトリ内での名前 (最長 47 バイト) で，経路は親を
# 辿って組み立てる。sfs1 は経路まるごとを 1 個の名前にしていた。
#
# **sfs1 は残してある。** Stage 12〜15 の検査がすべてそれに依っており，
# 凍結済みの成果物の再現に要る。
set -eu

die() { echo "sfs2.sh: $*" >&2; exit 1; }

# u32 リトルエンディアンの読み書き
r32() { od -An -tu4 -j "$2" -N 4 "$1" | tr -d ' '; }
w32() {
    _b0=$(($3 & 255)); _b1=$(($3 >> 8 & 255))
    _b2=$(($3 >> 16 & 255)); _b3=$(($3 >> 24 & 255))
    # shellcheck disable=SC2059
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' "$_b0" "$_b1" "$_b2" "$_b3")" \
        | dd of="$1" bs=4 seek=$(($2 / 4)) conv=notrunc 2> /dev/null
}

# 表項目 (64 バイト): name[48], parent u32, dataoff u32, len u32, flags u32
TBLOFF=32
ENTSZ=64
NAMEMAX=47
F_USED=1
F_DIR=2

# 項目を 1 つ書く。$1=img $2=索引 $3=名前 $4=親 $5=位置 $6=長さ $7=flags
went() {
    _eo=$((TBLOFF + $2 * ENTSZ))
    if [ -n "$3" ]; then
        printf '%s' "$3" | dd of="$1" bs=1 seek="$_eo" conv=notrunc 2> /dev/null
    fi
    w32 "$1" $((_eo + 48)) "$4"
    w32 "$1" $((_eo + 52)) "$5"
    w32 "$1" $((_eo + 56)) "$6"
    w32 "$1" $((_eo + 60)) "$7"
}

pack() {
    dir=$1; img=$2; size=${3:-4194304}; cnt=${4:-256}
    [ -d "$dir" ] || die "no such directory: $dir"
    datastart=$((TBLOFF + cnt * ENTSZ))
    [ "$datastart" -lt "$size" ] || die "size too small for $cnt entries"

    rm -f "$img"
    dd if=/dev/null of="$img" bs=1 seek="$size" 2> /dev/null
    printf 'sfs2' | dd of="$img" bs=4 conv=notrunc 2> /dev/null
    w32 "$img" 4 "$size"
    w32 "$img" 8 "$TBLOFF"
    w32 "$img" 12 "$cnt"

    # 索引 0 はルート (名前は空，親は自分自身)
    went "$img" 0 '' 0 0 0 $((F_USED | F_DIR))
    i=1

    # ディレクトリを先に作れるよう，経路の浅い順・辞書順に並べる。
    # -type d と -type f を分けて出し，d を先に置く
    #
    # **深さの印は 0 詰めにする。** 詰めずに並べると文字列として比べられ，
    # "10" が "2" より前に来る。深さ 10 以上の木で親より先に子が出て，
    # pack が "parent not found" で落ちる (GCC 4.7.4 の木は深さ 12)。
    # tcc の木は深さ 6 だったので，これまで表に出ていなかった
    list=$(mktemp)
    (cd "$dir" && { find . -mindepth 1 -type d | sed 's|^\./||' \
                        | awk -v FS=/ '{printf "%03d/%s\n", NF, $0}' \
                        | LC_ALL=C sort | cut -d/ -f2-
                    find . -mindepth 1 -type f | sed 's|^\./||' | LC_ALL=C sort; }) > "$list"

    # 経路 -> 索引 の対応表 (ルートは空文字)
    idx=$(mktemp)
    printf '\t0\n' > "$idx"

    cur=$datastart
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        [ "$i" -lt "$cnt" ] || die "too many entries (max $cnt)"
        case "$path" in
        */*) parent=${path%/*}; name=${path##*/} ;;
        *)   parent=''; name=$path ;;
        esac
        nbytes=$(printf '%s' "$name" | wc -c | tr -d ' \t')
        [ "$nbytes" -le "$NAMEMAX" ] || die "name too long ($nbytes bytes): $name"
        pi=$(awk -F'\t' -v p="$parent" '$1 == p { print $2; exit }' "$idx")
        [ -n "$pi" ] || die "parent not found: $parent (for $path)"
        if [ -d "$dir/$path" ]; then
            went "$img" "$i" "$name" "$pi" 0 0 $((F_USED | F_DIR))
            printf '%s\t%s\n' "$path" "$i" >> "$idx"
        else
            len=$(wc -c < "$dir/$path" | tr -d ' \t')
            [ $((cur + len)) -le "$size" ] || die "image full at: $path"
            went "$img" "$i" "$name" "$pi" "$cur" "$len" "$F_USED"
            if [ "$len" -gt 0 ]; then
                dd if="$dir/$path" of="$img" bs=64K oflag=seek_bytes seek="$cur" \
                    conv=notrunc 2> /dev/null
            fi
            cur=$(((cur + len + 3) / 4 * 4))    # 4 バイト境界へ切上げ
        fi
        i=$((i + 1))
    done < "$list"
    rm -f "$list" "$idx"
    w32 "$img" 16 "$cur"
}

check_magic() {
    [ "$(dd if="$1" bs=4 count=1 2> /dev/null)" = 'sfs2' ] \
        || die "not an sfs2 image: $1"
}

entname() {
    dd if="$1" bs=1 skip="$2" count=48 2> /dev/null | tr '\0' '\n' | head -n 1
}

# イメージの全項目を "索引 <TAB> 親 <TAB> flags <TAB> 位置 <TAB> 長さ <TAB> 名前"
# の形で出す。呼び手が経路を組み立てる
entries() {
    _tbl=$(r32 "$1" 8); _cnt=$(r32 "$1" 12)
    _i=0
    while [ "$_i" -lt "$_cnt" ]; do
        _eo=$((_tbl + _i * ENTSZ))
        _fl=$(r32 "$1" $((_eo + 60)))
        if [ $((_fl & F_USED)) -ne 0 ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$_i" \
                "$(r32 "$1" $((_eo + 48)))" "$_fl" \
                "$(r32 "$1" $((_eo + 52)))" "$(r32 "$1" $((_eo + 56)))" \
                "$(entname "$1" "$_eo")"
        fi
        _i=$((_i + 1))
    done
}

# 索引 -> 経路 を組み立てて "経路 <TAB> flags <TAB> 位置 <TAB> 長さ" を出す。
# 親は必ず自分より小さい索引にあるので，1 度なめれば足りる
paths() {
    entries "$1" | awk -F'\t' '
        NR == 1 { path[$1] = ""; next }        # 索引 0 = ルート
        {
            p = path[$2]
            full = (p == "") ? $6 : p "/" $6
            path[$1] = full
            printf "%s\t%s\t%s\t%s\n", full, $3, $4, $5
        }'
}

unpack() {
    img=$1; dir=$2
    check_magic "$img"
    mkdir -p "$dir"
    paths "$img" | while IFS="$(printf '\t')" read -r path fl off len; do
        # 経路の検査 (イメージは信用しない)
        case "/$path/" in
        */../*|//*|*//*) die "bad path in image: $path" ;;
        esac
        if [ $((fl & F_DIR)) -ne 0 ]; then
            mkdir -p "$dir/$path"
        else
            case "$path" in
            */*) mkdir -p "$dir/${path%/*}" ;;
            esac
            if [ "$len" -gt 0 ]; then
                dd if="$img" of="$dir/$path" bs=64K \
                    iflag=skip_bytes,count_bytes skip="$off" count="$len" \
                    2> /dev/null
            else
                : > "$dir/$path"
            fi
        fi
    done
}

list() {
    check_magic "$1"
    paths "$1" | while IFS="$(printf '\t')" read -r path fl off len; do
        if [ $((fl & F_DIR)) -ne 0 ]; then
            printf 'd %8s %s\n' - "$path"
        else
            printf 'f %8s %s\n' "$len" "$path"
        fi
    done
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
pack)   pack "$@" ;;
unpack) unpack "$@" ;;
list)   list "$@" ;;
*)      echo "usage: sfs2.sh {pack <dir> <img> [size] [maxent] | unpack <img> <dir> | list <img>}" >&2
        exit 2 ;;
esac
