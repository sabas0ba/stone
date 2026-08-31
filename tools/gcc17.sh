#!/bin/sh
# GCC 4.7.4 を Stage 17 の測定対象として扱う。
#
#   gcc17.sh measure   取得済みソースと現在の sfs3/kernel24 の容量を比較する
#
# ソースは tools/fetch.sh gcc47 で docs/external/gcc47 に取得する。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src="$repo_root/docs/external/gcc47"

die() {
    echo "gcc17.sh: $*" >&2
    exit 1
}

constant() {
    awk -F= -v name="$2" '$1 == name { print $2; exit }' "$1"
}

define() {
    awk -v name="$2" '$1 == "#define" && $2 == name { print $3; exit }' "$1"
}

measure() {
    [ -d "$src" ] || die "GCC 4.7.4 が無い (sh tools/fetch.sh gcc47)"

    files=$(find "$src" -type f -printf '.\n' | wc -l | tr -d ' ')
    directories=$(find "$src" -type d -printf '.\n' | wc -l | tr -d ' ')
    entries=$((files + directories - 1))

    size_stats=$(find "$src" -type f -printf '%s\n' | awk '
        {
            bytes += $1
            padded += int(($1 + 3) / 4) * 4
        }
        END { printf "%.0f %.0f", bytes, padded }
    ')
    file_bytes=${size_stats%% *}
    padded_file_bytes=${size_stats#* }

    max_name_bytes=$(find "$src" -mindepth 1 -printf '%f\n' \
        | LC_ALL=C awk 'length($0) > max { max = length($0) } END { print max }')
    max_path_bytes=$(find "$src" -mindepth 1 -printf '%P\n' \
        | LC_ALL=C awk 'length($0) > max { max = length($0) } END { print max }')
    max_depth=$(find "$src" -mindepth 1 -printf '%P\n' \
        | LC_ALL=C awk -F/ 'NF > max { max = NF } END { print max }')

    sfs_name_limit=$(constant "$repo_root/tools/sfs3.sh" NAMEMAX)
    table_offset=$(constant "$repo_root/tools/sfs3.sh" TBLOFF)
    entry_size=$(constant "$repo_root/tools/sfs3.sh" ENTSZ)
    [ -n "$sfs_name_limit" ] || die "sfs3 の名前上限を読めない"
    [ -n "$table_offset" ] || die "sfs3 の表位置を読めない"
    [ -n "$entry_size" ] || die "sfs3 の項目幅を読めない"

    names_over_limit=$(find "$src" -mindepth 1 -printf '%f\n' \
        | LC_ALL=C awk -v limit="$sfs_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')
    cxx_header_max_name_bytes=$(find "$src/libstdc++-v3/include" -mindepth 1 \
        -printf '%f\n' | LC_ALL=C awk '
            length($0) > max { max = length($0) }
            END { print max }
        ')
    cxx_headers_over_limit=$(find "$src/libstdc++-v3/include" -mindepth 1 \
        -printf '%f\n' | LC_ALL=C awk -v limit="$sfs_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')

    table_entries=$((entries + 1))
    table_bytes=$((table_offset + table_entries * entry_size))
    minimum_image_bytes=$((table_bytes + padded_file_bytes))

    sfsa=$(define "$repo_root/stage017/kernel24.c" SFSA)
    ubase=$(define "$repo_root/stage017/kernel24.c" UBASE)
    [ -n "$sfsa" ] || die "kernel24 の SFSA を読めない"
    [ -n "$ubase" ] || die "kernel24 の UBASE を読めない"
    sfs_window_bytes=$((ubase - sfsa))

    fits_name_limit=yes
    [ "$names_over_limit" -eq 0 ] || fits_name_limit=no
    fits_window=yes
    [ "$minimum_image_bytes" -le "$sfs_window_bytes" ] || fits_window=no

    cat <<EOF
source=gcc-4.7.4
files=$files
directories=$directories
entries=$entries
file-bytes=$file_bytes
padded-file-bytes=$padded_file_bytes
max-name-bytes=$max_name_bytes
max-path-bytes=$max_path_bytes
max-depth=$max_depth
sfs3-name-limit=$sfs_name_limit
names-over-limit=$names_over_limit
libstdcxx-header-max-name-bytes=$cxx_header_max_name_bytes
libstdcxx-headers-over-limit=$cxx_headers_over_limit
sfs3-table-entries=$table_entries
sfs3-table-bytes=$table_bytes
sfs3-minimum-image-bytes=$minimum_image_bytes
kernel24-sfs-window-bytes=$sfs_window_bytes
fits-name-limit=$fits_name_limit
fits-kernel24-window=$fits_window
EOF
}

cmd=${1:-}
case "$cmd" in
measure) measure ;;
*)
    echo "usage: gcc17.sh measure" >&2
    exit 2
    ;;
esac
