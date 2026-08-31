#!/bin/sh
# sfs4 イメージとディレクトリの相互変換 (ホスト側の道具)。
# 形式は docs/stage017-gcc.md 7 章。
#
# 使用法:
#   sfs4.sh pack <dir> <img> [size] [maxent]   ディレクトリ -> イメージ
#   sfs4.sh unpack <img> <dir>                 イメージ -> ディレクトリ
#   sfs4.sh list <img>                         一覧 (種別・長さ・時刻・経路)
#
# 既定: size = 4194304 (4 MiB), maxent = 256。
# SFS4_PROGRESS が空でなければ、1000 entriesごとに進捗をstderrへ出す。
#
# sfs3 (tools/sfs3.sh) から name を 48 -> 128 バイトへ広げた。
# GCC 4.7.4 の配布木で実測した最長 component は 92 バイトである。
# 項目は 72 -> 152 バイトになる。時刻の配置は末尾へ移るが意味は同じ。
#
# **秒に直さずナノ秒のまま持つ。** カーネルが goldfish RTC から取る値が
# ナノ秒であり，秒に直すには 64 bit の除算が要る (docs/stage017-cc.md
# 11.2)。ホスト側もそれに合わせる。
#
# **sfs3 / sfs2 / sfs1 は残してある。** Stage 12〜17 の検査がそれらに
# 依っており，凍結済みの成果物の再現に要る。
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
die() { echo "sfs4.sh: $*" >&2; exit 1; }

# u32 リトルエンディアンの読出し
r32() { od -An -tu4 -j "$2" -N 4 "$1" | tr -d ' '; }

# 表項目 (152 バイト): name[128], parent, dataoff, len, flags, mtlo, mthi
TBLOFF=32
ENTSZ=152
NAMEMAX=127
E_PAR=128
E_OFF=132
E_LEN=136
E_FLAG=140
E_MTLO=144
E_MTHI=148
F_USED=1
F_DIR=2

pack() {
    size=${3:-4194304}
    cnt=${4:-256}
    exec perl "$script_dir/sfs4-pack.pl" "$1" "$2" "$size" "$cnt"
}

check_magic() {
    [ "$(dd if="$1" bs=4 count=1 2> /dev/null)" = 'sfs4' ] \
        || die "not an sfs4 image: $1"
}

entname() {
    dd if="$1" bs=1 skip="$2" count=128 2> /dev/null | tr '\0' '\n' | head -n 1
}

# 全項目を "索引 親 flags 位置 長さ 時刻(ns) 名前" (TAB 区切り) で出す
entries() {
    _tbl=$(r32 "$1" 8); _cnt=$(r32 "$1" 12)
    _i=0
    while [ "$_i" -lt "$_cnt" ]; do
        _eo=$((_tbl + _i * ENTSZ))
        _fl=$(r32 "$1" $((_eo + E_FLAG)))
        if [ $((_fl & F_USED)) -ne 0 ]; then
            _lo=$(r32 "$1" $((_eo + E_MTLO))); _hi=$(r32 "$1" $((_eo + E_MTHI)))
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_i" \
                "$(r32 "$1" $((_eo + E_PAR)))" "$_fl" \
                "$(r32 "$1" $((_eo + E_OFF)))" "$(r32 "$1" $((_eo + E_LEN)))" \
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
    exec perl "$script_dir/sfs4-list.pl" "$1"
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
pack)   pack "$@" ;;
unpack) unpack "$@" ;;
list)   list "$@" ;;
*)      echo "usage: sfs4.sh {pack <dir> <img> [size] [maxent] | unpack <img> <dir> | list <img>}" >&2
        exit 2 ;;
esac
