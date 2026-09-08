#!/bin/sh
# sfs3 イメージとディレクトリの相互変換 (ホスト側の道具)。
# 形式は docs/stage017-cc.md 11.3。
#
# 使用法:
#   sfs3.sh pack <dir> <img> [size] [maxent]   ディレクトリ -> イメージ
#   sfs3.sh unpack <img> <dir>                 イメージ -> ディレクトリ
#   sfs3.sh list <img>                         一覧 (種別・長さ・時刻・経路)
#
# 既定: size = 4194304 (4 MiB), maxent = 256。
#
# sfs2 (tools/sfs2.sh) との違いは**項目が更新時刻を持つ**ことだけである。
# 項目は 64 -> 72 バイトに広がり，末尾に mtlo / mthi (epoch からの
# ナノ秒を u32 2 本で持つ) が付いた。
#
# **秒に直さずナノ秒のまま持つ。** カーネルが goldfish RTC から取る値が
# ナノ秒であり，秒に直すには 64 bit の除算が要る (docs/stage017-cc.md
# 11.2)。ホスト側もそれに合わせる。
#
# **sfs2 / sfs1 は残してある。** Stage 12〜16 の検査がそれらに依っており，
# 凍結済みの成果物の再現に要る。
set -eu

die() { echo "sfs3.sh: $*" >&2; exit 1; }

# u32 リトルエンディアンの読み書き
r32() { od -An -tu4 -j "$2" -N 4 "$1" | tr -d ' '; }
w32() {
    _b0=$(($3 & 255)); _b1=$(($3 >> 8 & 255))
    _b2=$(($3 >> 16 & 255)); _b3=$(($3 >> 24 & 255))
    # shellcheck disable=SC2059
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' "$_b0" "$_b1" "$_b2" "$_b3")" \
        | dd of="$1" bs=4 seek=$(($2 / 4)) conv=notrunc 2> /dev/null
}

# 表項目 (72 バイト): name[48], parent, dataoff, len, flags, mtlo, mthi
TBLOFF=32
ENTSZ=72
NAMEMAX=47
F_USED=1
F_DIR=2

# ファイルの mtime をナノ秒で取る。**%.9Y は GNU stat の拡張**なので，
# 無ければ秒だけ取って 1e9 倍する
#
# 小数部の頭の 0 は自分で落とす。`10#` は bash の拡張で，このスクリプトを
# 読む sh (dash) では算術の誤りになる
mtime_ns() {
    _v=$(stat -c '%.9Y' "$1" 2> /dev/null || true)
    case "$_v" in
    *.*)
        _s=${_v%.*}; _f=${_v#*.}
        while [ ${#_f} -lt 9 ]; do _f="${_f}0"; done
        _f=$(printf '%s' "$_f" | cut -c1-9 | sed 's/^0*//')
        [ -n "$_f" ] || _f=0
        echo $(( _s * 1000000000 + _f ))
        ;;
    *)
        _s=$(stat -c '%Y' "$1") || die "cannot stat: $1"
        echo $(( _s * 1000000000 ))
        ;;
    esac
}

# 項目を 1 つ書く。
# $1=img $2=索引 $3=名前 $4=親 $5=位置 $6=長さ $7=flags $8=時刻(ns)
went() {
    _eo=$((TBLOFF + $2 * ENTSZ))
    if [ -n "$3" ]; then
        printf '%s' "$3" | dd of="$1" bs=1 seek="$_eo" conv=notrunc 2> /dev/null
    fi
    w32 "$1" $((_eo + 48)) "$4"
    w32 "$1" $((_eo + 52)) "$5"
    w32 "$1" $((_eo + 56)) "$6"
    w32 "$1" $((_eo + 60)) "$7"
    w32 "$1" $((_eo + 64)) $(( $8 % 4294967296 ))
    w32 "$1" $((_eo + 68)) $(( $8 / 4294967296 ))
}

pack() {
    dir=$1; img=$2; size=${3:-4194304}; cnt=${4:-256}
    [ -d "$dir" ] || die "no such directory: $dir"
    datastart=$((TBLOFF + cnt * ENTSZ))
    [ "$datastart" -lt "$size" ] || die "size too small for $cnt entries"

    rm -f "$img"
    dd if=/dev/null of="$img" bs=1 seek="$size" 2> /dev/null
    printf 'sfs3' | dd of="$img" bs=4 conv=notrunc 2> /dev/null
    w32 "$img" 4 "$size"
    w32 "$img" 8 "$TBLOFF"
    w32 "$img" 12 "$cnt"

    # 索引 0 はルート (名前は空，親は自分自身)
    went "$img" 0 '' 0 0 0 $((F_USED | F_DIR)) "$(mtime_ns "$dir")"
    i=1

    # ディレクトリを先に作れるよう，経路の浅い順・辞書順に並べる
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
        ns=$(mtime_ns "$dir/$path")
        if [ -d "$dir/$path" ]; then
            went "$img" "$i" "$name" "$pi" 0 0 $((F_USED | F_DIR)) "$ns"
            printf '%s\t%s\n' "$path" "$i" >> "$idx"
        else
            len=$(wc -c < "$dir/$path" | tr -d ' \t')
            [ $((cur + len)) -le "$size" ] || die "image full at: $path"
            went "$img" "$i" "$name" "$pi" "$cur" "$len" "$F_USED" "$ns"
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
    [ "$(dd if="$1" bs=4 count=1 2> /dev/null)" = 'sfs3' ] \
        || die "not an sfs3 image: $1"
}

entname() {
    dd if="$1" bs=1 skip="$2" count=48 2> /dev/null | tr '\0' '\n' | head -n 1
}

# 全項目を "索引 親 flags 位置 長さ 時刻(ns) 名前" (TAB 区切り) で出す
entries() {
    _tbl=$(r32 "$1" 8); _cnt=$(r32 "$1" 12)
    _i=0
    while [ "$_i" -lt "$_cnt" ]; do
        _eo=$((_tbl + _i * ENTSZ))
        _fl=$(r32 "$1" $((_eo + 60)))
        if [ $((_fl & F_USED)) -ne 0 ]; then
            _lo=$(r32 "$1" $((_eo + 64))); _hi=$(r32 "$1" $((_eo + 68)))
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_i" \
                "$(r32 "$1" $((_eo + 48)))" "$_fl" \
                "$(r32 "$1" $((_eo + 52)))" "$(r32 "$1" $((_eo + 56)))" \
                "$((_hi * 4294967296 + _lo))" \
                "$(entname "$1" "$_eo")"
        fi
        _i=$((_i + 1))
    done
}

# 索引 -> 経路 を組み立てて "経路 flags 位置 長さ 時刻" を出す
paths() {
    entries "$1" | awk -F'\t' '
        NR == 1 { path[$1] = ""; next }        # 索引 0 = ルート
        {
            p = path[$2]
            full = (p == "") ? $7 : p "/" $7
            path[$1] = full
            printf "%s\t%s\t%s\t%s\t%s\n", full, $3, $4, $5, $6
        }'
}

# ナノ秒を touch -d が食う形 (@秒.ナノ秒) にする
stamp() {
    _ns=$1
    printf '@%s.%09d' $((_ns / 1000000000)) $((_ns % 1000000000))
}

unpack() {
    img=$1; dir=$2
    check_magic "$img"
    mkdir -p "$dir"
    # ディレクトリの時刻は中身を作った後で当て直す。先に当てると
    # 子を作った時点で上書きされる
    dirs=$(mktemp)
    paths "$img" | while IFS="$(printf '\t')" read -r path fl off len ns; do
        # 経路の検査 (イメージは信用しない)
        case "/$path/" in
        */../*|//*|*//*) die "bad path in image: $path" ;;
        esac
        if [ $((fl & F_DIR)) -ne 0 ]; then
            mkdir -p "$dir/$path"
            printf '%s\t%s\n' "$path" "$ns" >> "$dirs"
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
            touch -d "$(stamp "$ns")" "$dir/$path"
        fi
    done
    # 深い順に当てると親を触らずに済む。
    #
    # **深さを数えるのに $1 を書き換えてはいけない。** gsub は対象を
    # その場で書き換え，awk は $1 を触ると $0 を OFS (既定は空白) で
    # 組み立て直す。区切りの TAB が空白に化けて，経路と時刻が 1 つの
    # 語になり，"sub 1787501759947168325" という名前のファイルができた
    if [ -s "$dirs" ]; then
        awk -F'\t' '{ n = $1; d = gsub(/\//, "/", n); print d "\t" $0 }' "$dirs" \
            | LC_ALL=C sort -rn | cut -f2- \
            | while IFS="$(printf '\t')" read -r path ns; do
                  touch -d "$(stamp "$ns")" "$dir/$path" \
                      || die "cannot set time on: $path"
              done
    fi
    rm -f "$dirs"
}

list() {
    check_magic "$1"
    paths "$1" | while IFS="$(printf '\t')" read -r path fl off len ns; do
        if [ $((fl & F_DIR)) -ne 0 ]; then
            printf 'd %8s %20s %s\n' - "$ns" "$path"
        else
            printf 'f %8s %20s %s\n' "$len" "$ns" "$path"
        fi
    done
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
pack)   pack "$@" ;;
unpack) unpack "$@" ;;
list)   list "$@" ;;
*)      echo "usage: sfs3.sh {pack <dir> <img> [size] [maxent] | unpack <img> <dir> | list <img>}" >&2
        exit 2 ;;
esac
