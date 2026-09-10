#!/bin/sh
# GCC 4.7.4 を Stage 17 の測定対象として扱う。
#
#   gcc17.sh measure   取得済みソースと現在の sfs4/kernel25 の容量を比較する
#   gcc17.sh pack      全配布木を窓と同じ大きさの sfs4 に実際に詰める (手動)
#
#   gcc17.sh configure            libiberty / libcpp を host で configure し config.h を作る
#   gcc17.sh headers [lib]        単位ごとに header の閉包を取り，我々の libc に無いものを数える
#   gcc17.sh closure <lib>/<unit> 1 単位の閉包 (無い header は空の代役で埋める) を出す
#   gcc17.sh unit <lib>/<unit>    1 単位を bundle -> pp16 -> cc15v に通し，結果を 1 行で出す
#   gcc17.sh units [lib]          全単位を通し，tmp/g17u/units.txt に表を出す (6.1 の 4)
#
# ソースは tools/fetch.sh gcc47 で docs/external/gcc47 に取得する。
# unit / units は鎖の像 (tmp/build) と QEMU を要る。STONE_ENGINE と
# qemu-system-riscv32 は呼ぶ側の環境で与える (tools/env.sh の契約のまま)。
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

# ---- 翻訳単位を我々の器に読ませる (6.1 の 4) ----
#
# tcc のときと同じ測り方である (docs/stage017-gcc.md 4.1) —— ソースを
# 読ませ，**通らなかった単位とその理由を数える**。その表の長さが，
# 我々の器と GCC の C との距離になる。
#
# 対象は libiberty と libcpp である。GCC 本体 (gcc/) は configure が
# 生成する header (tm.h / insn-*.h) を要り，しかも 4.7.4 には riscv の
# backend が無いので host 向けに configure するしかない。まず純粋な C で
# 書かれた 2 つの書庫で測る。

work="$repo_root/tmp/g17u"
ours="$repo_root/stage015/libc/include"
pp16=tmp/build/pp16.bin
cc15=tmp/build/cc15v.bin        # 最前線の世代で測る (tools/diff17.sh と同じ)
shim="$repo_root/tests/hostshim/shim-gcc.h"
HOSTCC=${CC:-gcc}

lib_dirs() {
    case $1 in
    libiberty) echo "-I$work/libiberty -I$src/libiberty -I$src/include" ;;
    libcpp)    echo "-I$work/libcpp -I$src/libcpp -I$src/libcpp/include -I$src/include" ;;
    *) die "未知の書庫: $1 (libiberty | libcpp)" ;;
    esac
}

# config.h を host で作る。
#
# **configure を我々の OS で回すのは 4.2 の別件である。** ここで要るのは
# 単位を読むための config.h だけなので，host の autoconf に作らせる。
# ただし host の header と語長で作ると，我々に無い header を「ある」と
# 書いた config.h になる。2 つ手当てする。
#
#   1. CPPFLAGS で header の探し道を我々の libc だけにする。autoconf の
#      AC_CHECK_HEADERS は「その header を含む試験を訳せるか」で決めるので，
#      HAVE_*_H が我々の header の有無を映す
#   2. 語長は autoconf の cache 変数で RV32 の値を与える。host は 64 bit
#      なので，放っておくと SIZEOF_LONG が 8 になる
#
# **関数の有無 (HAVE_STRERROR など) は host の link 試験で決まる。** ここは
# 手当てしていない。効くのは代替実装を選ぶ枝だけで，読ませる単位の一覧
# (REQUIRED_OFILES / libcpp_a_OBJS) には影響しない。
configure() {
    [ -d "$src" ] || die "GCC 4.7.4 が無い: $src (sh tools/fetch.sh gcc47)"
    for lib in libiberty libcpp; do
        mkdir -p "$work/$lib"
        (cd "$work/$lib" \
         && CPPFLAGS="-nostdinc -isystem $ours" \
            ac_cv_sizeof_short=2 ac_cv_sizeof_int=4 ac_cv_sizeof_long=4 \
            ac_cv_sizeof_long_long=8 ac_cv_sizeof_void_p=4 ac_cv_c_bigendian=no \
            sh "$src/$lib/configure" --srcdir="$src/$lib" > configure.log 2>&1) \
            || die "$lib の configure が落ちた ($work/$lib/configure.log)"
        [ -s "$work/$lib/config.h" ] || die "$lib の config.h ができていない"
        echo "configured $lib ($work/$lib/config.h)"
    done
    # libcpp/init.c が読む localedir.h は libc の header ではなく，make が
    # Makefile の localedir から作る (libcpp/Makefile.in 144 行)。make は
    # 回さないので，同じ 1 行をここで作る。無いと「libc に無い header」に
    # 数えられてしまう
    echo '#define LOCALEDIR "/usr/local/share/locale"' > "$work/libcpp/localedir.h"
}

# 書庫が -c する翻訳単位の一覧。Makefile.in の変数から取る (自分で選ばない)
unit_list() {
    case $1 in
    libiberty)
        # @pexecute@ は configure が OS ごとの実装 (pex-unix など) に置き換える
        pex=$(sed -n 's/^pexecute *= *//p' "$work/libiberty/Makefile")
        awk '/^REQUIRED_OFILES *=/ { f = 1 } f { print } f && !/\\$/ { exit }' \
            "$src/libiberty/Makefile.in" \
            | tr -s ' \t\\' '\n' | grep -E '\.\$\(objext\)$' \
            | sed 's|^\./||; s|\.\$(objext)$||' | sed "s|^@pexecute@\$|$pex|"
        ;;
    libcpp)
        awk '/^libcpp_a_OBJS *=/ { f = 1 } f { print } f && !/\\$/ { exit }' \
            "$src/libcpp/Makefile.in" \
            | tr -s ' \t\\' '\n' | grep -E '\.o$' | sed 's|\.o$||'
        ;;
    esac
}

# header の閉包を host の cpp に出させる。成功なら経路を 1 行ずつ，
# 失敗なら無い header の名前を標準エラーへ出して 1 を返す。
#
#   -M     -MM ではない。-MM は system 扱いの header を省くが，我々の
#          libc の header も束ねに要る
#   -undef host の既定 macro (__GNUC__ / __linux__ / __x86_64__ …) を消す。
#          残すのは我々の pp が定義する __STONE__ だけである (__STDC__ は
#          消せない)。これで #if の枝が我々の pp と揃い，host だけが辿る
#          枝の header を拾わず，我々だけが辿る枝の header を落とさない
#   -std=c89
#          __STDC_VERSION__ を消す。我々の pp は定義しない
closure() {
    lib=${1%%/*}; u=${1#*/}
    [ -s "$work/$lib/config.h" ] || die "config.h が無い (sh tools/gcc17.sh configure)"
    mkdir -p "$work/out"
    m="$work/out/$lib.$u.M"
    # STONE_GCC17_STUB は headers が置く空の代役の階層。我々の libc の後ろ
    stubdir=""
    [ -n "${STONE_GCC17_STUB:-}" ] && stubdir="-I$STONE_GCC17_STUB"
    # shellcheck disable=SC2046,SC2086
    if ! "$HOSTCC" -M -std=c89 -undef -D__STONE__=1 -nostdinc -I"$ours" $stubdir \
            $(lib_dirs "$lib") -DHAVE_CONFIG_H "$src/$lib/$u.c" > "$m" 2> "$m.err"; then
        grep -m1 -oE 'fatal error: [^:]+: No such file' "$m.err" \
            | sed 's/fatal error: //; s/: No such file//' >&2
        return 1
    fi
    tr -s ' \\\n' '\n' < "$m" | grep -vE ':$|^$' | grep -v "/$u\.c$" | sort -u
}

# 閉包を，無い header を埋めながら閉じさせる。
#
# **cpp は最初に無かった header で止まる。** その先に何が無いかは判らない
# ので，無い header を**空の代役** (tmp/g17u/stub) で埋めながら閉じるまで
# 繰り返す。代役は我々の libc の後ろに置くので，在るものは本物が読まれる。
#
# 代役は単位をまたいで共有する (一度置けば次の単位は止まらない)。その単位に
# 無い header は，**閉じた閉包のうち代役の階層にある経路**として読み取る。
# 共有しても単位ごとに正確に出る。名前は $work/out/<単位>.missing に置く
# (この関数は $( ) の中で呼ばれるので，変数では返せない)。
#
# 閉じた閉包 (代役を含む) を標準出力に出す。代役を含めるのは，そのまま
# 束ねて pp に通すためである —— 空の代役でも型が在れば先へ進めるし，
# 無ければ host 側の検査が decl として名指しする
stub="$work/stub"
closure_all() {
    lib=${1%%/*}; u=${1#*/}
    mkdir -p "$stub" "$work/out"
    mf="$work/out/$lib.$u.missing"
    i=0
    while [ "$i" -lt 32 ]; do
        if out=$(STONE_GCC17_STUB="$stub" closure "$1" 2> "$work/out/hdr.err"); then
            printf '%s\n' "$out" | grep "^$stub/" | sed "s|^$stub/||" | tr '\n' ' ' \
                | sed 's/ $//' > "$mf"
            printf '%s\n' "$out"
            return 0
        fi
        h=$(cat "$work/out/hdr.err")
        # 名前が取れない失敗 (cpp の別の誤り) は "?" で残す
        [ -n "$h" ] || { echo "?" > "$mf"; return 1; }
        mkdir -p "$stub/$(dirname "$h")"
        : > "$stub/$h"
        i=$((i + 1))
    done
    echo "?" > "$mf"
    return 1
}

# 単位ごとに閉包を取り，我々の libc に無い header を名指しで数える。
# 鎖も QEMU も要らない。
#
# **無い header の数ではなく単位の数で数える** —— 1 つの header が何単位を
# 塞いでいるかが，埋める順番を決める
headers() {
    [ -s "$work/libiberty/config.h" ] || die "config.h が無い (sh tools/gcc17.sh configure)"
    t="$work/headers.txt"
    rm -rf "$stub"
    : > "$t"
    for lib in ${1:-libiberty libcpp}; do
        for u in $(unit_list "$lib"); do
            out=$(closure_all "$lib/$u") || true
            missing=$(cat "$work/out/$lib.$u.missing")
            if [ -z "$missing" ]; then
                printf '%s/%s\tok\t%s\n' "$lib" "$u" \
                    "$(printf '%s\n' "$out" | grep -vc "^$stub/")"
            else
                printf '%s/%s\thdr\t%s\n' "$lib" "$u" "$missing"
            fi
        done
    done | tee "$t"
    echo
    echo "headers: $(grep -c "$(printf '\tok\t')" "$t") 単位は閉包が閉じる / $(grep -c "$(printf '\thdr\t')" "$t") 単位は header が無い"
    echo "無い header (塞いでいる単位の数):"
    grep "$(printf '\thdr\t')" "$t" | cut -f3 | tr ' ' '\n' | sort | uniq -c | sort -rn | sed 's/^/  /'
}

# 我々の header そのものが host の C89 検査に引っかかる診断を，先に控えて
# おく。単位の診断からこれを引き，**単位の側の ISO C 違反だけ**を残す
# (我々の inttypes.h の long long を GCC のせいにしない)
baseline_iso() {
    b="$work/out/baseline.iso"
    [ -s "$b" ] && return 0
    mkdir -p "$work/out"
    for h in "$ours"/*.h "$ours"/sys/*.h; do
        printf '#include <%s>\n' "${h#"$ours"/}"
    done > "$work/out/baseline.c"
    # 診断が 1 つも無ければ grep が 1 を返し，空の baseline が正しい答である
    "$HOSTCC" -fsyntax-only -std=c89 -pedantic-errors -nostdinc -I"$ours" \
        -include "$shim" "$work/out/baseline.c" 2>&1 \
        | grep -oE 'error: ISO C[^[]*' | sort -u > "$b" || true
    return 0
}

# 1 単位を通す。結果は "書庫/単位 <状態> <詳細>" の 1 行 (TAB 区切り)。
#
#   ok    .o ができた (詳細は大きさ)
#   hdr   我々の libc に header が無い (詳細は名前)。**空の代役で埋めて先へ
#         進め，その結果を "-> <状態> <詳細>" として後ろに足す。** 型が
#         既に在れば通るし，無ければ decl として名指しされる。1 行で
#         「何が無いか」と「それを埋めたら何が起きるか」の両方が読める
#   cap   容量の上限 (pp16 の束ね 64 員 / cc の 6)
#   pp    我々の pp が落ちた (詳細は終了コード)
#   decl  我々の .i を host が GNU C としても通さない。宣言か型が我々の
#         libc に無いことがほとんどである (詳細は host の最初の診断)
#   ext   我々が拒み，host も C89 として拒む。GNU / C99 の拡張である
#         (詳細は host の ISO C 診断)。Stage 18 の的
#   gap   我々だけが拒む。**我々の C89 適合の穴** (詳細は cc の終了コード)
#
# 拒んだ理由を我々の cc は終了コードでしか言わないので，**同じ .i を host
# に読ませて**分類する (tools/diff17.sh と同じ手)。.i は我々の pp が我々の
# header で作ったものなので，host に含めさせる header は無い
unit() {
    lib=${1%%/*}; u=${1#*/}
    [ -s "$pp16" ] && [ -s "$cc15" ] || die "鎖の像が無い: $pp16 / $cc15 (sh tools/build.sh all)"
    mkdir -p "$work/out"
    o="$work/out/$lib.$u"
    hdrs=$(closure_all "$1") || true
    missing=$(cat "$work/out/$lib.$u.missing")
    if [ "$missing" = "?" ]; then
        printf '%s\thdr\t?\n' "$1"
        return 0
    fi
    # 無い header があれば，行の頭はそれで，先の結果を "->" で繋ぐ
    lead=""
    [ -n "$missing" ] && lead="hdr	$missing -> "
    printf '%s\t%s' "$1" "$lead"
    unit_run "$1" "$hdrs" "$o"
}

# 閉包 (代役を含む) を束ねて pp16 -> cc15v に通し，状態と詳細を出す
unit_run() {
    lib=${1%%/*}; u=${1#*/}
    hdrs=$2; o=$3
    # 束ねの員。我々の libc の header は include からの相対経路を名前にする
    # (<sys/time.h> は "sys/time.h" で探される)。代役も同じ。それ以外は
    # basename —— libiberty / libcpp / include の間に同名の header は無い
    members=""; n=1
    for h in $hdrs; do
        case $h in
        "$ours"/*) name=${h#"$ours"/} ;;
        "$stub"/*) name=${h#"$stub"/} ;;
        *)         name=$(basename "$h") ;;
        esac
        members="$members $name=$h"; n=$((n + 1))
    done
    # pp16 の束ねは 64 員 (mbname 4096 バイト / 64 バイト) が上限。超える
    # ものは走らせない (走らせても 6 で落ちるだけで，何も判らない)
    if [ "$n" -gt 64 ]; then
        printf 'cap\tpp members=%s\n' "$n"
        return 0
    fi

    # **落ちるのが本題である。** この script は set -e で走るので，拒む
    # ことを期待する呼び出しは if で受けて終了コードを取る。裸で置くと
    # rc=$? に届く前に script ごと終わり，units では tee の手前が消えて
    # 表が途中で切れる
    # shellcheck disable=SC2086
    if sh tools/bundle.sh $members "$src/$lib/$u.c" \
            | sh tools/env.sh qemu "$pp16" > "$o.i" 2> "$o.pp.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -ne 0 ]; then
        printf 'pp\t%s\n' "$rc"
        return 0
    fi

    if sh tools/env.sh qemu "$cc15" < "$o.i" > "$o.o" 2> "$o.cc.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -eq 0 ]; then
        printf 'ok\t%s\n' "$(wc -c < "$o.o" | tr -d ' ')"
        return 0
    fi
    if [ "$rc" -eq 6 ]; then
        printf 'cap\tcc 6\n'
        return 0
    fi

    # ここから host に訊く。-x c で前処理から通す (.i のままだと -include
    # が効かない)。我々の .i に指令は残っていないので，通し直しても変わらない
    if ! "$HOSTCC" -fsyntax-only -std=gnu89 -w -include "$shim" -x c "$o.i" \
            > "$o.h1.log" 2>&1; then
        printf 'decl\t%s\n' "$(grep -m1 -oE 'error: .*' "$o.h1.log")"
        return 0
    fi
    baseline_iso
    "$HOSTCC" -fsyntax-only -std=c89 -pedantic-errors -include "$shim" -x c "$o.i" \
        > "$o.h2.log" 2>&1 || true
    # ISO C の診断が 1 つも無ければ grep が 1 を返す。それも答である
    iso=$(grep -oE 'error: ISO C[^[]*' "$o.h2.log" | sort | uniq -c | sort -rn \
        | while read -r _ msg; do
              grep -qxF "$msg" "$work/out/baseline.iso" || { echo "$msg"; break; }
          done) || true
    if [ -n "$iso" ]; then
        printf 'ext\t%s\n' "$iso"
    else
        printf 'gap\t%s\n' "$rc"
    fi
}

units() {
    t="$work/units.txt"
    : > "$t"
    for lib in ${1:-libiberty libcpp}; do
        for u in $(unit_list "$lib"); do
            unit "$lib/$u"
        done
    done | tee "$t"
    echo
    echo "units: $(wc -l < "$t" | tr -d ' ') 単位"
    cut -f2 "$t" | sort | uniq -c | sort -rn | sed 's/^/  /'
}

cmd=${1:-}
case "$cmd" in
measure) measure ;;
pack) pack ;;
configure) configure ;;
headers) headers "${2:-}" ;;
closure) [ -n "${2:-}" ] || die "closure <lib>/<unit>"; closure_all "$2"; cat "$work/out/${2%%/*}.${2#*/}.missing" >&2 ;;
unit) [ -n "${2:-}" ] || die "unit <lib>/<unit>"; unit "$2" ;;
units) units "${2:-}" ;;
*)
    echo "usage: gcc17.sh {measure | pack | configure | headers [lib] | closure <lib>/<unit> | unit <lib>/<unit> | units [lib]}" >&2
    exit 2
    ;;
esac
