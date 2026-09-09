#!/bin/sh
# GCC 4.7.4 を Stage 17 の測定対象として扱う。
#
#   gcc17.sh measure   取得済みソースと現在の sfs4/kernel25 の容量を比較する
#   gcc17.sh pack      全配布木を窓と同じ大きさの sfs4 に実際に詰める (手動)
#
# ソースは tools/fetch.sh gcc47 で docs/external/gcc47 に取得する。
#
# STONE_GCC47_SRC で測る木を差し替えられる。**答の判っている小さな木で
# 算術そのものを検査するため**である (tests/stage017 第 5 部)。GCC の木が
# 手元に無い環境でも，式が合っているかはそれで確かめられる。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# 作業用の表の項目数。
#
# **最小 image は「読むだけ」の値である。** 表を実測ちょうど (81,356) で
# 切ると，ゲストは file を 1 つも作れない。GCC を組む間に出る .o や .a は
# 表に項目を要るので，詰める側は最初から余りを持たせる。
#
# 2^17 は実測の 81,356 を超える最小の 2 の冪で，49,716 項目の余りが残る。
# 項目幅が 128 バイトなので表は 16 MiB 丁度になり，窓 512 MiB のうち
# data 領域に 52,491,992 バイトの余りが残る (docs/stage017-gcc.md 7.4)。
#
# STONE_SFS4_WORKSPACE_ENTRIES で下げられる。**pack の経路そのものを
# 現実的な時間で検査するため**である。この shell 実装は表の空き枠も
# 1 つずつ走査するので，131,072 枠のままでは検査に数十分かかる。
WORKSPACE_ENTRIES=${STONE_SFS4_WORKSPACE_ENTRIES:-131072}
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

    # 作業用に表を広げた場合。**表は先に切ってしまうので，余りは data
    # 領域だけに残る。** ここが GCC を組む間に出る生成物の置き場になる
    workspace_table_bytes=$((table_offset + WORKSPACE_ENTRIES * entry_size))
    workspace_used_bytes=$((workspace_table_bytes + padded_file_bytes))
    workspace_free_entries=$((WORKSPACE_ENTRIES - table_entries))
    workspace_headroom_bytes=$((sfs_window_bytes - workspace_used_bytes))

    fits_name_limit=yes
    [ "$names_over_limit" -eq 0 ] || fits_name_limit=no
    fits_entry_types=yes
    fits_window=yes
    fits_workspace=yes
    if [ "$unsupported_entries" -ne 0 ]; then
        minimum_image_bytes=unknown
        workspace_used_bytes=unknown
        workspace_headroom_bytes=unknown
        fits_entry_types=no
        fits_window=unknown
        fits_workspace=unknown
    else
        [ "$minimum_image_bytes" -le "$sfs_window_bytes" ] || fits_window=no
        # 余りが負なら，木そのものが予約した項目数に入っていない
        [ "$workspace_free_entries" -ge 0 ] || fits_workspace=no
        [ "$workspace_headroom_bytes" -ge 0 ] || fits_workspace=no
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
sfs4-workspace-table-entries=$WORKSPACE_ENTRIES
sfs4-workspace-free-entries=$workspace_free_entries
sfs4-workspace-used-bytes=$workspace_used_bytes
sfs4-workspace-headroom-bytes=$workspace_headroom_bytes
fits-entry-types=$fits_entry_types
fits-name-limit=$fits_name_limit
fits-kernel25-window=$fits_window
fits-kernel25-workspace=$fits_workspace
EOF
}

# 全配布木を，窓と同じ大きさの sfs4 に実際に詰めて，詰めた結果を検算する。
#
# **見積りと実物は別である。** measure は表の幅と詰めた大きさから下限を
# 出すだけで，pack がその規模を通せるかは見ていない。深さ 12・81,355 経路
# という規模では，並び順や親の索引付けなど計算に出ない所で落ちうる
# (実際に深さ 10 以上で落ちる誤りが後から見つかっている)。
#
# **CI では回さない。** tools/sfs4.sh の pack は項目ごとに dd と od を呼ぶ
# POSIX shell であり，81,356 項目では 1 時間半かかる (実測 1h27m)。手で
# 測るための手順として置く。進行は SFS4_PROGRESS で stderr へ出る
# (docs/stage017-gcc.md 7.5)。
pack() {
    [ -d "$src" ] || die "詰める木が無い: $src (sh tools/fetch.sh gcc47)"

    # sfs4 が表現できない entry があれば，詰めた結果は木と一致しない。
    # 数だけ数えて先へ進むと「全部載った」と誤読するので，名指しで止める
    link=$(find "$src" -type l -print -quit)
    other=$(find "$src" ! -type f ! -type d ! -type l -print -quit)
    [ -z "$link" ] || die "symbolic link は sfs4 に載らない: $link"
    [ -z "$other" ] || die "未対応の entry は sfs4 に載らない: $other"

    files=$(find "$src" -type f -printf '.\n' | wc -l | tr -d ' ')
    directories=$(find "$src" -type d -printf '.\n' | wc -l | tr -d ' ')
    table_entries=$((files + directories))
    paths=$((table_entries - 1))            # ルートは経路を持たない
    padded_file_bytes=$(find "$src" -type f -printf '%s\n' | awk '
        { padded += int(($1 + 3) / 4) * 4 }
        END { printf "%.0f", padded }
    ')

    table_offset=$(constant "$repo_root/tools/sfs4.sh" TBLOFF)
    entry_size=$(constant "$repo_root/tools/sfs4.sh" ENTSZ)
    [ -n "$table_offset" ] || die "sfs4 の表位置を読めない"
    [ -n "$entry_size" ] || die "sfs4 の項目幅を読めない"

    # ゲストが載せられる上限そのもので詰める。ここを超えた image は
    # kernel25 が S を出して拒む (docs/stage017-gcc.md 7.2)
    sfsa=$(define "$repo_root/stage017/kernel25.c" SFSA)
    sfstop=$(define "$repo_root/stage017/kernel25.c" SFSTOP)
    [ -n "$sfsa" ] || die "kernel25 の SFSA を読めない"
    [ -n "$sfstop" ] || die "kernel25 の SFSTOP を読めない"
    image_bytes=$((sfstop - sfsa))

    out="$repo_root/tmp/g17"
    image="$out/gcc47.sfs4"
    mkdir -p "$out"
    SFS4_PROGRESS=1 sh "$repo_root/tools/sfs4.sh" pack "$src" "$image" \
        "$image_bytes" "$WORKSPACE_ENTRIES"

    [ "$(dd if="$image" bs=4 count=1 2> /dev/null)" = sfs4 ] \
        || die "詰めた後の magic が sfs4 でない"

    # 頭の 16 バイト目は data の書き込み位置である。表を先に切ってから
    # 詰めた分だけ進むので，見積りと 1 バイトも違わないはずである
    cursor=$(od -An -tu4 -j 16 -N 4 "$image" | tr -d ' ')
    expected=$((table_offset + WORKSPACE_ENTRIES * entry_size \
        + padded_file_bytes))
    [ "$cursor" -eq "$expected" ] \
        || die "使用量が一致しない (expected=$expected image=$cursor)"

    # 詰めた image を読み直して，種別と経路の集合をそのまま突き合わせる。
    #
    # **数を数えるだけでは足りない。** 親の索引が 1 つずれても，項目は
    # 有効なまま残るので list は同じ行数を出す。数が合ったまま，guest から
    # 見える木だけが別物になる。同じ理由で，重複した経路や種別の取り違えも
    # 数には出ない。ここは親の索引付けを通すための検査なので，集合で比べる。
    want="$out/want.txt"
    got="$out/got.txt"
    (cd "$src" && { find . -mindepth 1 -type d -printf 'd\t%P\n'
                    find . -mindepth 1 -type f -printf 'f\t%P\n'; }) \
        | LC_ALL=C sort > "$want"
    # list は "種別 長さ 時刻 経路" を空白で揃えて出す。経路には空白が
    # ありうるので，前の 3 語だけを落として残りをそのまま経路とする
    sh "$repo_root/tools/sfs4.sh" list "$image" | awk '
        {
            end = index($0, $3) + length($3) + 1
            printf "%s\t%s\n", $1, substr($0, end)
        }' | LC_ALL=C sort > "$got"
    diff -u "$want" "$got" > "$out/paths.diff" \
        || die "経路の集合が一致しない ($out/paths.diff を見る)"
    packed_paths=$(wc -l < "$got" | tr -d ' ')
    [ "$packed_paths" -eq "$paths" ] \
        || die "経路の数が一致しない (source=$paths image=$packed_paths)"

    cat <<EOF
image=$image
image-bytes=$image_bytes
used-bytes=$cursor
headroom-bytes=$((image_bytes - cursor))
table-entries=$WORKSPACE_ENTRIES
free-entries=$((WORKSPACE_ENTRIES - table_entries))
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
