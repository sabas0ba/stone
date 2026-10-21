#!/bin/sh
# Linux 5.4 の C の単位を我々の cc に通して測る (docs/stage019-linux.md 6 章)。
#
#   linux19.sh units <kbout>        <kbout> の記録 (tools/kbuildlog.sh) から C の単位を拾う
#   linux19.sh pre <kbout>          全単位を前処理だけする (scan の材料。cc は通さない)
#   linux19.sh one <kbout> <obj>    1 単位を前処理して cc に通し，表の 1 行を出す
#   linux19.sh all <kbout>          全単位を通して表に足していく (途中から再開できる)
#   linux19.sh scan <kbout>         前処理した単位の中の拡張の使われ方を数える
#   linux19.sh spre <kbout>         .S の単位を前処理だけする (nas1 の入力。docs/stage018-ncc.md 10 章の M1)
#   linux19.sh ncc <kbout>          前処理した全単位をホストの ncc1 と nas1 に通す (同 M4)
#
# <kbout> は tools/kbuildlog.sh run の出力先 (Kbuild の objtree) である。
# 単位ごとの命令行はそこの kblog/tools.log にある。
#
# **前処理はホストの cpp に任せる。** 我々の pp はまだ -I の探索順を持たない
# (バンドルの名前で引く)。ここで測りたいのは cc が GNU C の拡張をどこまで
# 受けるかなので，前処理を切り離す。ホストの cpp には -undef と -nostdinc を
# 渡してホストの定義を消し，stage019/predef-rv32-gcc46.h (GCC 4.6.4 / riscv32 が
# 前もって定義するもの) と stage019/include (処理系の stdarg.h) を与える。
# 単位の -I / -D / -U / -include はそのまま渡す。
#
# 表の列: 単位 (.o) / 前処理 (ok か失敗) / cc の終了コード / .i の大きさ / cc の秒数
#
# 作業の置き場は <kbout> ごとに分ける (tmp/linux19/<kbout の名前>)。別の
# objtree の命令行や .i が混ざると，測定が黙って誤る
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
base="$repo_root/tmp/linux19"
work=""
predef="$repo_root/stage019/predef-rv32-gcc46.h"
ccinc="$repo_root/stage019/include"
cc15="tmp/build/cc15as.bin"
ncc_host="$repo_root/tmp/nas1/host"
HOSTCC=${HOSTCC:-gcc}

die() { echo "linux19.sh: $*" >&2; exit 1; }

# C の単位 (-c で __ASSEMBLY__ でないもの) の命令行を 1 単位 1 ファイルに分ける
# 第 2 引数が asm なら .S の単位 (-D__ASSEMBLY__ のもの) を拾う
units() {
    kb=$1
    kind=${2:-c}
    log="$kb/kblog/tools.log"
    [ -s "$log" ] || die "記録が無い: $log (sh tools/kbuildlog.sh run)"
    rm -rf "$work/args"
    mkdir -p "$work/args"
    : > "$work/units.tsv"
    if [ "$kind" = asm ]; then
        sel='-D__ASSEMBLY__'
        neg=''
    else
        sel=' -c '
        neg='-D__ASSEMBLY__'
    fi
    # cc-option の試し (-c -x c /dev/null -o .tmp_*/tmp) は単位ではない
    grep '^gcc ' "$log" | grep -- ' -c ' | grep -- "$sel" | grep -v -e "${neg:-^$}" \
        | grep -v -- ' /dev/null' | grep -v -- ' -o \.tmp_' | grep -v -- ' -o /tmp/' \
        | while IFS= read -r line; do
            obj=$(printf '%s\n' "$line" | tr ' ' '\n' | awk 'p { print; exit } $0 == "-o" { p = 1 }')
            key=$(printf '%s' "$obj" | tr '/' '_')
            printf '%s\n' "${line#gcc }" > "$work/args/$key"
            printf '%s\t%s\n' "$obj" "$key" >> "$work/units.tsv"
        done
    echo "units: $(wc -l < "$work/units.tsv" | tr -d ' ') 単位 ($work/units.tsv)"
}

# 単位の命令行から前処理に効く選択肢だけを残し，ホストの cpp で .i を作る
preprocess() {
    kb=$1; key=$2; out=$3
    set -f
    # shellcheck disable=SC2046
    set -- $(cat "$work/args/$key")
    set +f
    src=""
    : > "$out.args"
    while [ $# -gt 0 ]; do
        case "$1" in
        -I|-isystem|-include|-D|-U)
            # 処理系の include の階層は我々のものに替える
            if [ "$1" = -isystem ] && [ "$2" = "$kb/kblog/include" ]; then
                printf '%s\n%s\n' -isystem "$ccinc" >> "$out.args"
            else
                printf '%s\n%s\n' "$1" "$2" >> "$out.args"
            fi
            shift 2
            continue
            ;;
        -I*|-D*|-U*|-nostdinc)
            printf '%s\n' "$1" >> "$out.args"
            ;;
        -mcmodel=medany)
            # 単位ごとに code model を変えるものがある (arch/riscv/mm/init.o)。
            # 処理系が定義するマクロも替わる
            printf '%s\n%s\n' -U__riscv_cmodel_medlow -D__riscv_cmodel_medany=1 >> "$out.args"
            ;;
        -o|-MF|-MT|-MQ|--param|-x)
            shift
            ;;
        -*)
            ;;
        *)
            src=$1
            ;;
        esac
        shift
    done
    [ -n "$src" ] || return 1
    # .S は拡張子でホストの cpp が assembler-with-cpp として読む。-std は C のときだけ
    case "$src" in
    *.S) stdopt='' ;;
    *) stdopt='-std=gnu89' ;;
    esac
    # 選択肢は 1 行 1 個で持っているので，改行で区切って渡す
    (
        cd "$kb"
        IFS='
'
        set -f
        # shellcheck disable=SC2046
        "$HOSTCC" -E -P -undef -nostdinc $stdopt -include "$predef" \
            $(cat "$out.args") "$src" -o "$out" 2> "$out.cpp.err"
    )
}

one() {
    kb=$1; obj=$2
    key=$(printf '%s' "$obj" | tr '/' '_')
    [ -s "$work/args/$key" ] || die "単位が無い: $obj (sh tools/linux19.sh units)"
    mkdir -p "$work/out"
    o="$work/out/$key"
    if ! preprocess "$kb" "$key" "$o.i"; then
        printf '%s\tcpp\t-\t-\t-\n' "$obj"
        return 0
    fi
    # 我々の cc は 0x04 を入力の終わりとして読む (pp が付けるもの)
    printf '\004' >> "$o.i"
    t0=$(date +%s)
    if sh tools/env.sh qemu "$cc15" < "$o.i" > "$o.o" 2> "$o.cc.err"; then
        rc=0
    else
        rc=$?
    fi
    t1=$(date +%s)
    printf '%s\tok\t%s\t%s\t%ss\n' "$obj" "$rc" "$(wc -c < "$o.i" | tr -d ' ')" "$((t1 - t0))"
}

# 全単位。表に既にある単位は飛ばすので，止まっても同じ命令で続きから進む
all() {
    kb=$1
    [ -s "$work/units.tsv" ] || units "$kb"
    t="$work/table.tsv"
    touch "$t"
    total=$(wc -l < "$work/units.tsv" | tr -d ' ')
    n=0
    while IFS="$(printf '\t')" read -r obj _key; do
        n=$((n + 1))
        if awk -F '\t' -v u="$obj" '$1 == u { f = 1 } END { exit !f }' "$t"; then
            continue
        fi
        one "$kb" "$obj" < /dev/null >> "$t"
        printf '%s/%s: %s\n' "$n" "$total" "$(tail -n 1 "$t")" >&2
    done < "$work/units.tsv"
    echo "all: $(wc -l < "$t" | tr -d ' ') 単位 ($t)"
    cut -f 2,3 "$t" | sort | uniq -c | sort -rn
}

# 全単位を前処理だけする。cc は QEMU の上で 1 単位に数分かかるので，
# 拡張の使われ方 (scan) を先に数えるための材料を作る。単位の一覧も .i も
# 毎回作り直す (前の回の .i が残ると scan が数えてしまう)
pre_all() {
    kb=$1
    units "$kb"
    rm -rf "$work/out"
    mkdir -p "$work/out"
    ok=0; ng=0
    while IFS="$(printf '\t')" read -r obj key; do
        if preprocess "$kb" "$key" "$work/out/$key.i" < /dev/null; then
            ok=$((ok + 1))
        else
            ng=$((ng + 1))
            echo "cpp で失敗: $obj ($work/out/$key.i.cpp.err)" >&2
        fi
    done < "$work/units.tsv"
    echo "pre: 前処理できた $ok / 失敗 $ng"
}

# .S の単位を前処理する。-D__ASSEMBLY__ は単位の命令行が持つ。作業の置き場は
# C の単位と分ける (<work>/asm)。.s は nas1 の試験の入力になる
spre_all() {
    kb=$1
    work="$work/asm"
    units "$kb" asm
    rm -rf "$work/out"
    mkdir -p "$work/out"
    ok=0; ng=0
    while IFS="$(printf '\t')" read -r obj key; do
        if preprocess "$kb" "$key" "$work/out/$key.s" < /dev/null; then
            ok=$((ok + 1))
        else
            ng=$((ng + 1))
            echo "cpp で失敗: $obj ($work/out/$key.s.cpp.err)" >&2
        fi
    done < "$work/units.tsv"
    echo "spre: 前処理できた $ok / 失敗 $ng ($work/out)"
}

# 前処理した単位の中で，C89 の外の形がいくつの単位に現れるかを数える。
# 字面で数える目安であり，構文として確かめてはいない
scan() {
    [ -d "$work/out" ] || die "前処理した単位が無い (sh tools/linux19.sh pre)"
    total=$(find "$work/out" -name '*.i' | wc -l | tr -d ' ')
    echo "前処理した単位: $total"
    while IFS='|' read -r label pattern; do
        [ -n "$label" ] || continue
        c=$(find "$work/out" -name '*.i' -print0 | xargs -0 grep -lE -- "$pattern" 2> /dev/null | wc -l | tr -d ' ')
        printf '%5s  %s\n' "$c" "$label"
    done <<'EOF'
__attribute__|__attribute__
inline asm (asm / __asm__)|(^|[^A-Za-z0-9_])(asm|__asm__|__asm)[[:space:]]*(volatile|__volatile__|goto)?[[:space:]]*\(
asm goto|(asm|__asm__)[[:space:]]+(volatile[[:space:]]+|__volatile__[[:space:]]+)?goto
文式 ({ ... })|\(\{
typeof / __typeof__|(^|[^A-Za-z0-9_])(typeof|__typeof__|__typeof)[[:space:]]*\(
__builtin_constant_p|__builtin_constant_p
__builtin_expect|__builtin_expect
__builtin_va_*|__builtin_va_
__builtin_choose_expr / types_compatible_p|__builtin_(choose_expr|types_compatible_p)
__builtin_offsetof|__builtin_offsetof
__builtin_unreachable|__builtin_unreachable
その他の __builtin_|__builtin_(memcpy|memset|memcmp|strlen|bswap|clz|ctz|ffs|popcount|return_address|frame_address|trap|prefetch|object_size|alloca|add_overflow|sub_overflow|mul_overflow)
指定付き初期化子 .x =|[{,][[:space:]]*\.[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=
配列の指定付き初期化子 [n] =|[{,][[:space:]]*\[[^]]+\][[:space:]]*=
case の範囲 ...|case[^:]*\.\.\.
__extension__|__extension__
inline / __inline__|(^|[^A-Za-z0-9_])(inline|__inline__|__inline)([^A-Za-z0-9_]|$)
long long|long[[:space:]]+long
長さ 0 の配列 [0]|\[0\];
_Static_assert|_Static_assert
__alignof__|__alignof__
__label__|__label__
計算した goto (goto *)|goto[[:space:]]*\*
EOF
}

# 前処理した 1 単位をホストで組んだ ncc1 と nas1 に通す。結果は
# <work>/ncc/<key>.{s,o,err,rc}。rc は ncc1 の終了コード，ncc1 が通れば
# nas1 の終了コードを 100 足して重ねる (0 は両方通った)
ncc_one() {
    key=$1
    d="$work/ncc"
    rm -f "$d/$key.s" "$d/$key.o"
    set +e
    (ulimit -v 2000000; timeout 60 "$ncc_host/ncc1" -o "$d/$key.s" "$work/out/$key.i") 2> "$d/$key.err"
    rc=$?
    if [ $rc -eq 0 ]; then
        "$ncc_host/nas1" -o "$d/$key.o" "$d/$key.s" 2>> "$d/$key.err"
        r2=$?
        [ $r2 -eq 0 ] || rc=$((100 + r2))
    fi
    set -e
    echo $rc > "$d/$key.rc"
}

# 前処理した全単位を ncc1 と nas1 に通し，終了コードと最初の誤りの数を出す
# (docs/stage018-ncc.md 10 章の M4)。ncc1 と nas1 は tools/ncc18.sh host が
# 組んだものを使う
ncc_all() {
    [ -d "$work/out" ] || die "前処理した単位が無い (sh tools/linux19.sh pre)"
    [ -x "$ncc_host/ncc1" ] && [ -x "$ncc_host/nas1" ] || die "ncc1 / nas1 が無い (sh tools/ncc18.sh host)"
    rm -rf "$work/ncc"
    mkdir -p "$work/ncc"
    find "$work/out" -name '*.i' | sed 's|.*/||; s|\.i$||' | sort \
        | xargs -P "${JOBS:-8}" -I{} sh "$0" ncc-one "$kbabs" {}
    total=$(find "$work/ncc" -name '*.rc' | wc -l | tr -d ' ')
    ok=$(grep -lx 0 "$work/ncc"/*.rc | wc -l | tr -d ' ')
    echo "ncc: 通った $ok / $total ($work/ncc)"
    cat "$work/ncc"/*.rc | sort | uniq -c | sort -rn
    for f in "$work/ncc"/*.err; do
        head -n 1 "$f"
    done | sed 's/^[^:]*: [^:]*:[0-9]*: //' | sort | uniq -c | sort -rn | head -n 30
}

# <kbout> を絶対経路にし，作業の置き場をそれに結び付ける
use_tree() {
    [ -d "$1" ] || die "objtree が無い: $1"
    kbabs=$(CDPATH='' cd -- "$1" && pwd)
    work="$base/$(basename "$kbabs")"
}

cmd=${1:-}
[ $# -ge 2 ] && use_tree "$2"
case "$cmd" in
units) [ $# -ge 2 ] || die "units <kbout>"; units "$kbabs" ;;
pre) [ $# -ge 2 ] || die "pre <kbout>"; pre_all "$kbabs" ;;
one) [ $# -ge 3 ] || die "one <kbout> <obj>"; one "$kbabs" "$3" ;;
all) [ $# -ge 2 ] || die "all <kbout>"; all "$kbabs" ;;
scan) [ $# -ge 2 ] || die "scan <kbout>"; scan ;;
spre) [ $# -ge 2 ] || die "spre <kbout>"; spre_all "$kbabs" ;;
ncc) [ $# -ge 2 ] || die "ncc <kbout>"; ncc_all ;;
ncc-one) [ $# -ge 3 ] || die "ncc-one <kbout> <key>"; ncc_one "$3" ;;
*)
    echo "usage: linux19.sh {units <kbout> | pre <kbout> | one <kbout> <obj> | all <kbout> | scan <kbout> | spre <kbout> | ncc <kbout>}" >&2
    exit 2
    ;;
esac
