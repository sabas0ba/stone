#!/bin/sh
# GCC 4.7.4 を Stage 17 の測定対象として扱う。
#
#   gcc17.sh measure   取得済みソースを sfs3/kernel24 と sfs4/kernel25 に当てる
#   gcc17.sh pack      GCC 4.7.4 の全配布木を 1 GiB の sfs4 に詰める
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

    sfs3_name_limit=$(constant "$repo_root/tools/sfs3.sh" NAMEMAX)
    sfs3_table_offset=$(constant "$repo_root/tools/sfs3.sh" TBLOFF)
    sfs3_entry_size=$(constant "$repo_root/tools/sfs3.sh" ENTSZ)
    sfs4_name_limit=$(constant "$repo_root/tools/sfs4.sh" NAMEMAX)
    sfs4_table_offset=$(constant "$repo_root/tools/sfs4.sh" TBLOFF)
    sfs4_entry_size=$(constant "$repo_root/tools/sfs4.sh" ENTSZ)
    [ -n "$sfs3_name_limit" ] || die "sfs3 の名前上限を読めない"
    [ -n "$sfs3_table_offset" ] || die "sfs3 の表位置を読めない"
    [ -n "$sfs3_entry_size" ] || die "sfs3 の項目幅を読めない"
    [ -n "$sfs4_name_limit" ] || die "sfs4 の名前上限を読めない"
    [ -n "$sfs4_table_offset" ] || die "sfs4 の表位置を読めない"
    [ -n "$sfs4_entry_size" ] || die "sfs4 の項目幅を読めない"

    sfs3_names_over_limit=$(find "$src" -mindepth 1 -printf '%f\n' \
        | LC_ALL=C awk -v limit="$sfs3_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')
    sfs4_names_over_limit=$(find "$src" -mindepth 1 -printf '%f\n' \
        | LC_ALL=C awk -v limit="$sfs4_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')
    cxx_header_max_name_bytes=$(find "$src/libstdc++-v3/include" -mindepth 1 \
        -printf '%f\n' | LC_ALL=C awk '
            length($0) > max { max = length($0) }
            END { print max }
        ')
    cxx_headers_over_limit=$(find "$src/libstdc++-v3/include" -mindepth 1 \
        -printf '%f\n' | LC_ALL=C awk -v limit="$sfs3_name_limit" '
            length($0) > limit { count++ }
            END { print count + 0 }
        ')

    # sfs3 pack が表へ載せるのは regular file と directory だけである。
    # hard link は各 path の内容を regular file として materialize するが、
    # symbolic link その他の型は表現できない。後者があれば容量の下限を
    # 数字だけ出すと「全treeを収容できる」と誤読できるため unknown とする。
    table_entries=$((files + directories))
    sfs3_table_bytes=$((sfs3_table_offset + table_entries * sfs3_entry_size))
    sfs3_minimum_image_bytes=$((sfs3_table_bytes + padded_file_bytes))
    sfs4_table_bytes=$((sfs4_table_offset + table_entries * sfs4_entry_size))
    sfs4_minimum_image_bytes=$((sfs4_table_bytes + padded_file_bytes))
    sfs4_workspace_table_entries=131072
    sfs4_workspace_table_bytes=$((sfs4_table_offset \
        + sfs4_workspace_table_entries * sfs4_entry_size))
    sfs4_workspace_used_bytes=$((sfs4_workspace_table_bytes + padded_file_bytes))
    sfs4_workspace_free_entries=$((sfs4_workspace_table_entries - table_entries))

    sfsa=$(define "$repo_root/stage017/kernel24.c" SFSA)
    ubase=$(define "$repo_root/stage017/kernel24.c" UBASE)
    [ -n "$sfsa" ] || die "kernel24 の SFSA を読めない"
    [ -n "$ubase" ] || die "kernel24 の UBASE を読めない"
    kernel24_sfs_window_bytes=$((ubase - sfsa))

    sfsa4=$(define "$repo_root/stage017/kernel25.c" SFSA)
    sfsend4=$(define "$repo_root/stage017/kernel25.c" SFSEND)
    [ -n "$sfsa4" ] || die "kernel25 の SFSA を読めない"
    [ -n "$sfsend4" ] || die "kernel25 の SFSEND を読めない"
    kernel25_sfs_window_bytes=$((sfsend4 - sfsa4))

    fits_sfs3_name_limit=yes
    [ "$sfs3_names_over_limit" -eq 0 ] || fits_sfs3_name_limit=no
    fits_sfs4_name_limit=yes
    [ "$sfs4_names_over_limit" -eq 0 ] || fits_sfs4_name_limit=no
    fits_entry_types=yes
    fits_kernel24_window=yes
    fits_kernel25_window=yes
    if [ "$unsupported_entries" -ne 0 ]; then
        sfs3_minimum_image_bytes=unknown
        sfs4_minimum_image_bytes=unknown
        sfs4_workspace_used_bytes=unknown
        sfs4_workspace_headroom_bytes=unknown
        fits_entry_types=no
        fits_kernel24_window=unknown
        fits_kernel25_window=unknown
    else
        sfs4_workspace_headroom_bytes=$((kernel25_sfs_window_bytes \
            - sfs4_workspace_used_bytes))
        [ "$sfs3_minimum_image_bytes" -le "$kernel24_sfs_window_bytes" ] \
            || fits_kernel24_window=no
        [ "$sfs4_workspace_used_bytes" -le "$kernel25_sfs_window_bytes" ] \
            || fits_kernel25_window=no
    fi

    cat <<EOF
source=gcc-4.7.4
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
sfs3-name-limit=$sfs3_name_limit
names-over-limit=$sfs3_names_over_limit
libstdcxx-header-max-name-bytes=$cxx_header_max_name_bytes
libstdcxx-headers-over-limit=$cxx_headers_over_limit
sfs3-table-entries=$table_entries
sfs3-table-bytes=$sfs3_table_bytes
sfs3-minimum-image-bytes=$sfs3_minimum_image_bytes
kernel24-sfs-window-bytes=$kernel24_sfs_window_bytes
fits-entry-types=$fits_entry_types
fits-name-limit=$fits_sfs3_name_limit
fits-kernel24-window=$fits_kernel24_window
sfs4-name-limit=$sfs4_name_limit
sfs4-names-over-limit=$sfs4_names_over_limit
sfs4-table-entries=$table_entries
sfs4-table-bytes=$sfs4_table_bytes
sfs4-minimum-image-bytes=$sfs4_minimum_image_bytes
kernel25-sfs-window-bytes=$kernel25_sfs_window_bytes
sfs4-workspace-table-entries=$sfs4_workspace_table_entries
sfs4-workspace-free-entries=$sfs4_workspace_free_entries
sfs4-workspace-used-bytes=$sfs4_workspace_used_bytes
sfs4-workspace-headroom-bytes=$sfs4_workspace_headroom_bytes
fits-sfs4-name-limit=$fits_sfs4_name_limit
fits-kernel25-window=$fits_kernel25_window
EOF
}

pack() {
    [ -d "$src" ] || die "GCC 4.7.4 が無い (sh tools/fetch.sh gcc47)"

    symbolic_links=$(find "$src" -type l -print -quit)
    other_unsupported=$(find "$src" ! -type f ! -type d ! -type l -print -quit)
    [ -z "$symbolic_links" ] || die "symbolic link は sfs4 に載らない: $symbolic_links"
    [ -z "$other_unsupported" ] \
        || die "未対応entryは sfs4 に載らない: $other_unsupported"

    files=$(find "$src" -type f -printf '.\n' | wc -l | tr -d ' ')
    directories=$(find "$src" -type d -printf '.\n' | wc -l | tr -d ' ')
    source_table_entries=$((files + directories))
    paths=$((source_table_entries - 1))
    table_entries=131072
    sfsa=$(define "$repo_root/stage017/kernel25.c" SFSA)
    sfsend=$(define "$repo_root/stage017/kernel25.c" SFSEND)
    [ -n "$sfsa" ] || die "kernel25 の SFSA を読めない"
    [ -n "$sfsend" ] || die "kernel25 の SFSEND を読めない"
    image_bytes=$((sfsend - sfsa))

    out="$repo_root/tmp/g17"
    image="$out/gcc47.sfs4"
    mkdir -p "$out"
    SFS4_PROGRESS=1 sh "$repo_root/tools/sfs4.sh" pack "$src" "$image" \
        "$image_bytes" "$table_entries"

    [ "$(dd if="$image" bs=4 count=1 2> /dev/null)" = sfs4 ] \
        || die "pack後のmagicがsfs4でない"
    cursor=$(od -An -tu4 -j 16 -N 4 "$image" | tr -d ' ')
    entry_size=$(constant "$repo_root/tools/sfs4.sh" ENTSZ)
    table_offset=$(constant "$repo_root/tools/sfs4.sh" TBLOFF)
    padded_file_bytes=$(find "$src" -type f -printf '%s\n' | awk '
        { padded += int(($1 + 3) / 4) * 4 }
        END { printf "%.0f", padded }
    ')
    expected_cursor=$((table_offset + table_entries * entry_size \
        + padded_file_bytes))
    [ "$cursor" -eq "$expected_cursor" ] \
        || die "使用量が一致しない (expected=$expected_cursor image=$cursor)"
    packed_paths=$(sh "$repo_root/tools/sfs4.sh" list "$image" | wc -l \
        | tr -d ' ')
    [ "$packed_paths" -eq "$paths" ] \
        || die "path数が一致しない (source=$paths image=$packed_paths)"

    cat <<EOF
image=$image
image-bytes=$image_bytes
used-bytes=$cursor
table-entries=$table_entries
free-entries=$((table_entries - source_table_entries))
paths=$packed_paths
EOF
}

cmd=${1:-}
case "$cmd" in
measure) measure ;;
pack) pack ;;
*)
    echo "usage: gcc17.sh {measure | pack}" >&2
    exit 2
    ;;
esac
