#!/bin/sh
# GCC 4.7.4 を Stage 17 の測定対象として扱う。
#
#   gcc17.sh measure   取得済みソースと現在の sfs4/kernel25 の容量を比較する
#
# ソースは tools/fetch.sh gcc47 で docs/external/gcc47 に取得する。
#
# STONE_GCC47_SRC で測る木を差し替えられる。**答の判っている小さな木で
# 算術そのものを検査するため**である (tests/stage017 第 5 部)。GCC の木が
# 手元に無い環境でも，式が合っているかはそれで確かめられる。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -n "${STONE_GCC47_SRC:-}" ]; then
    src=$STONE_GCC47_SRC
    src_name=$(basename "$src")
else
    src="$repo_root/docs/external/gcc47"
    src_name=gcc-4.7.4
fi

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
    [ -d "$src" ] || die "測る木が無い: $src (sh tools/fetch.sh gcc47)"

    files=$(find "$src" -type f -printf '.\n' | wc -l | tr -d ' ')
    directories=$(find "$src" -type d -printf '.\n' | wc -l | tr -d ' ')
    symbolic_links=$(find "$src" -type l -printf '.\n' | wc -l | tr -d ' ')
    other_unsupported_entries=$(find "$src" ! -type f ! -type d ! -type l \
        -printf '.\n' | wc -l | tr -d ' ')
    unsupported_entries=$((symbolic_links + other_unsupported_entries))
    hard_linked_files=$(find "$src" -type f -links +1 -printf '.\n' \
        | wc -l | tr -d ' ')
    entries=$((files + directories + unsupported_entries - 1))

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

    sfs_name_limit=$(constant "$repo_root/tools/sfs4.sh" NAMEMAX)
    table_offset=$(constant "$repo_root/tools/sfs4.sh" TBLOFF)
    entry_size=$(constant "$repo_root/tools/sfs4.sh" ENTSZ)
    [ -n "$sfs_name_limit" ] || die "sfs4 の名前上限を読めない"
    [ -n "$table_offset" ] || die "sfs4 の表位置を読めない"
    [ -n "$entry_size" ] || die "sfs4 の項目幅を読めない"

    names_over_limit=$(find "$src" -mindepth 1 -printf '%f\n' \
        | LC_ALL=C awk -v limit="$sfs_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')
    # libstdc++ の header は**案 A で最初に要る**ので別に数える。
    # STONE_GCC47_SRC で小さな木を測るときは無いので，そのときは 0 と出す
    cxx_inc="$src/libstdc++-v3/include"
    if [ -d "$cxx_inc" ]; then
        cxx_header_max_name_bytes=$(find "$cxx_inc" -mindepth 1 \
            -printf '%f\n' | LC_ALL=C awk '
                length($0) > max { max = length($0) }
                END { print max + 0 }
            ')
        cxx_headers_over_limit=$(find "$cxx_inc" -mindepth 1 \
            -printf '%f\n' | LC_ALL=C awk -v limit="$sfs_name_limit" '
                length($0) > limit { count++ }
                END { print count + 0 }
            ')
    else
        cxx_header_max_name_bytes=0
        cxx_headers_over_limit=0
    fi

    # sfs4 pack が表へ載せるのは regular file と directory だけである。
    # hard link は各 path の内容を regular file として materialize するが、
    # symbolic link その他の型は表現できない。後者があれば容量の下限を
    # 数字だけ出すと「全treeを収容できる」と誤読できるため unknown とする。
    table_entries=$((files + directories))
    table_bytes=$((table_offset + table_entries * entry_size))
    minimum_image_bytes=$((table_bytes + padded_file_bytes))

    # kernel24 では窓の上端が UBASE (ユーザ像のロード先) だった。
    # kernel25 は像を退避領域より上へ移したので，上端は SFSTOP である
    sfsa=$(define "$repo_root/stage017/kernel25.c" SFSA)
    sfstop=$(define "$repo_root/stage017/kernel25.c" SFSTOP)
    [ -n "$sfsa" ] || die "kernel25 の SFSA を読めない"
    [ -n "$sfstop" ] || die "kernel25 の SFSTOP を読めない"
    sfs_window_bytes=$((sfstop - sfsa))

    fits_name_limit=yes
    [ "$names_over_limit" -eq 0 ] || fits_name_limit=no
    fits_entry_types=yes
    fits_window=yes
    if [ "$unsupported_entries" -ne 0 ]; then
        minimum_image_bytes=unknown
        fits_entry_types=no
        fits_window=unknown
    elif [ "$minimum_image_bytes" -gt "$sfs_window_bytes" ]; then
        fits_window=no
    fi

    cat <<EOF
source=$src_name
files=$files
directories=$directories
symbolic-links=$symbolic_links
other-unsupported-entries=$other_unsupported_entries
hard-linked-files=$hard_linked_files
entries=$entries
file-bytes=$file_bytes
padded-file-bytes=$padded_file_bytes
max-name-bytes=$max_name_bytes
max-path-bytes=$max_path_bytes
max-depth=$max_depth
sfs4-name-limit=$sfs_name_limit
names-over-limit=$names_over_limit
libstdcxx-header-max-name-bytes=$cxx_header_max_name_bytes
libstdcxx-headers-over-limit=$cxx_headers_over_limit
sfs4-table-entries=$table_entries
sfs4-table-bytes=$table_bytes
sfs4-minimum-image-bytes=$minimum_image_bytes
kernel25-sfs-window-bytes=$sfs_window_bytes
fits-entry-types=$fits_entry_types
fits-name-limit=$fits_name_limit
fits-kernel25-window=$fits_window
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
