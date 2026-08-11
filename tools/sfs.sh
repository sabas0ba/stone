#!/bin/sh
# sfs イメージとディレクトリの相互変換 (ホスト側の道具)。
# 形式は docs/stage012-os.md 4 章。
#
# 使用法:
#   sfs.sh pack <dir> <img> [size] [maxfiles]   ディレクトリ -> イメージ
#   sfs.sh unpack <img> <dir>                   イメージ -> ディレクトリ
#   sfs.sh list <img>                           一覧 (長さと名前)
#
# 既定: size = 4194304 (4 MiB), maxfiles = 128。
# 名前はディレクトリからの相対パス (最長 51 バイト)。
set -eu

die() { echo "sfs.sh: $*" >&2; exit 1; }

# u32 リトルエンディアンの読み書き
r32() { od -An -tu4 -j "$2" -N 4 "$1" | tr -d ' '; }
w32() {
    _b0=$(($3 & 255)); _b1=$(($3 >> 8 & 255))
    _b2=$(($3 >> 16 & 255)); _b3=$(($3 >> 24 & 255))
    # shellcheck disable=SC2059
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' "$_b0" "$_b1" "$_b2" "$_b3")" \
        | dd of="$1" bs=4 seek=$(($2 / 4)) conv=notrunc 2>/dev/null
}

# 表項目 (64 バイト) の配置: name[52], dataoff u32, len u32, flags u32
TBLOFF=32
ENTSZ=64

pack() {
    dir=$1; img=$2; size=${3:-4194304}; cnt=${4:-128}
    [ -d "$dir" ] || die "no such directory: $dir"
    datastart=$((TBLOFF + cnt * ENTSZ))
    [ "$datastart" -lt "$size" ] || die "size too small for $cnt entries"

    # 0 で満たした器 (スパース) を作り，スーパーブロックを書く
    rm -f "$img"
    dd if=/dev/null of="$img" bs=1 seek="$size" 2>/dev/null
    printf 'sfs1' | dd of="$img" bs=4 conv=notrunc 2>/dev/null
    w32 "$img" 4 "$size"
    w32 "$img" 8 "$TBLOFF"
    w32 "$img" 12 "$cnt"

    list=$(mktemp)
    (cd "$dir" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) > "$list"
    cur=$datastart
    i=0
    while IFS= read -r name; do
        [ "$i" -lt "$cnt" ] || die "too many files (max $cnt)"
        # 名前の上限はバイト数で決まる (表項目は name[52] + NUL 終端)。
        # ${#name} は文字数なので非 ASCII 名で検査が狂う
        nbytes=$(printf '%s' "$name" | wc -c | tr -d ' \t')
        [ "$nbytes" -le 51 ] || die "name too long ($nbytes bytes): $name"
        len=$(wc -c < "$dir/$name" | tr -d ' \t')
        [ $((cur + len)) -le "$size" ] || die "image full at: $name"
        eo=$((TBLOFF + i * ENTSZ))
        printf '%s' "$name" | dd of="$img" bs=1 seek="$eo" conv=notrunc 2>/dev/null
        w32 "$img" $((eo + 52)) "$cur"
        w32 "$img" $((eo + 56)) "$len"
        w32 "$img" $((eo + 60)) 1
        if [ "$len" -gt 0 ]; then
            dd if="$dir/$name" of="$img" bs=64K oflag=seek_bytes seek="$cur" \
                conv=notrunc 2>/dev/null
        fi
        cur=$(((cur + len + 3) / 4 * 4))    # 4 バイト境界へ切上げ
        i=$((i + 1))
    done < "$list"
    rm -f "$list"
    w32 "$img" 16 "$cur"
}

check_magic() {
    [ "$(dd if="$1" bs=4 count=1 2>/dev/null)" = 'sfs1' ] || die "not an sfs image: $1"
}

# 表項目の名前を読む (NUL 終端まで)
entname() { dd if="$1" bs=1 skip="$2" count=52 2>/dev/null | tr '\0' '\n' | head -n 1; }

unpack() {
    img=$1; dir=$2
    check_magic "$img"
    tbl=$(r32 "$img" 8); cnt=$(r32 "$img" 12)
    mkdir -p "$dir"
    i=0
    while [ "$i" -lt "$cnt" ]; do
        eo=$((tbl + i * ENTSZ))
        if [ "$(r32 "$img" $((eo + 60)))" = 1 ]; then
            name=$(entname "$img" "$eo")
            # パスの検査 (イメージは信用しない)
            case "/$name/" in
            */../*|//*|*//*) die "bad name in image: $name" ;;
            esac
            off=$(r32 "$img" $((eo + 52)))
            len=$(r32 "$img" $((eo + 56)))
            case "$name" in
            */*) mkdir -p "$dir/${name%/*}" ;;
            esac
            if [ "$len" -gt 0 ]; then
                dd if="$img" of="$dir/$name" bs=64K \
                    iflag=skip_bytes,count_bytes skip="$off" count="$len" 2>/dev/null
            else
                : > "$dir/$name"
            fi
        fi
        i=$((i + 1))
    done
}

list() {
    img=$1
    check_magic "$img"
    tbl=$(r32 "$img" 8); cnt=$(r32 "$img" 12)
    i=0
    while [ "$i" -lt "$cnt" ]; do
        eo=$((tbl + i * ENTSZ))
        if [ "$(r32 "$img" $((eo + 60)))" = 1 ]; then
            printf '%s %s\n' "$(r32 "$img" $((eo + 56)))" "$(entname "$img" "$eo")"
        fi
        i=$((i + 1))
    done
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
pack)   pack "$@" ;;
unpack) unpack "$@" ;;
list)   list "$@" ;;
*)      echo "usage: sfs.sh {pack <dir> <img> [size] [maxfiles] | unpack <img> <dir> | list <img>}" >&2
        exit 2 ;;
esac
