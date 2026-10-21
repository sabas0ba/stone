#!/bin/sh
# ncc1 の試験の駆動 (docs/stage018-ncc.md 7 章)。
#
#   ncc18.sh host                    nas1・nld1・ncc1 をホストの cc で組む (tmp/nas1/host/)
#   ncc18.sh expect [<test.c>...]    ホストの gcc で試験を組んで走らせ，期待 (.expect) を作る
#   ncc18.sh run <test.c>            ホスト版の ncc1・nas1・nld1 で組み，QEMU で走らせる
#   ncc18.sh check [<test.c>...]     run の出力を .expect と比べる (既定は tests/stage018/c/*.c)
#
# 試験の C は前処理器の指令を使わない (ncc1 は前処理済みの入力を読む)。
# 期待はホストの gcc で作る。RISC-V の char は unsigned なので
# -funsigned-char を付け，ILP32 と LP64 で違う long とポインタの幅を出力に
# 出さないように試験を書く。ホスト版は開発中の測定に使う
# (docs/stage019-linux.md 2 章)。正規の試験は tests/stage018/test.sh が
# stone の OS の上で組んだもので行う。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
# コンテナの中の QEMU にはリポジトリからの相対経路で渡す
work=tmp/nas1
HOSTCC=${HOSTCC:-gcc}

die() { echo "ncc18.sh: $*" >&2; exit 1; }

build_host() {
    mkdir -p "$work/host"
    for p in nas1 nld1 ncc1; do
        "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
            -o "$work/host/$p" "stage018/$p.c"
    done
    echo "built $work/host/{nas1,nld1,ncc1}" >&2
}

tests() {
    if [ $# -gt 0 ]; then
        printf '%s\n' "$@"
    else
        ls tests/stage018/c/*.c
    fi
}

# 期待はホストの gcc で作る。試験は libc の printf だけを使う。
# 組になる .s を持つ試験 (abi.c) はホストで組めないので，期待を手で書く
expect() {
    mkdir -p "$work/expect"
    tests "$@" | while IFS= read -r t; do
        b=$(basename "$t" .c)
        if [ -f "${t%.c}.s" ]; then
            echo "skip: $t (期待は手で書く)" >&2
            continue
        fi
        "$HOSTCC" -O2 -w -funsigned-char -fno-strict-aliasing -fwrapv -o "$work/expect/$b" "$t" \
            || die "ホストで組めない: $t"
        if "$work/expect/$b" > "${t%.c}.expect"; then
            echo "rc 0" >> "${t%.c}.expect"
        else
            echo "rc $?" >> "${t%.c}.expect"
        fi
        echo "expect: ${t%.c}.expect" >&2
    done
}

# ホスト版で組む: libc と試験を ncc1 -> nas1，start.s を nas1，nld1 で結合
link_host() {
    t=$1
    b=$(basename "$t" .c)
    d="$work/c"
    mkdir -p "$d"
    h="$work/host"
    "$h/nas1" -o "$d/start.o" tests/stage018/rt/start.s
    "$h/ncc1" -o "$d/libc.s" tests/stage018/rt/libc.c
    "$h/nas1" -o "$d/libc.o" "$d/libc.s"
    "$h/ncc1" -o "$d/$b.s" "$t"
    "$h/nas1" -o "$d/$b.o" "$d/$b.s"
    set -- "$d/start.o" "$d/libc.o" "$d/$b.o"
    if [ -f "${t%.c}.s" ]; then
        "$h/nas1" -o "$d/$b.asm.o" "${t%.c}.s"
        set -- "$@" "$d/$b.asm.o"
    fi
    "$h/nld1" -o "$d/$b.elf" "$@"
}

run_one() {
    t=$1
    b=$(basename "$t" .c)
    link_host "$t"
    if sh tools/env.sh qemu "$work/c/$b.elf" < /dev/null > "$work/c/$b.out" 2>&1; then
        echo "rc 0" >> "$work/c/$b.out"
    else
        echo "rc $?" >> "$work/c/$b.out"
    fi
}

check() {
    rc=0
    for t in $(tests "$@"); do
        b=$(basename "$t" .c)
        if ! (run_one "$t") > "$work/c.$b.log" 2>&1; then
            echo "FAIL  $b (組めない: $work/c.$b.log)"
            rc=1
            continue
        fi
        if diff -u "${t%.c}.expect" "$work/c/$b.out" > "$work/c/$b.diff"; then
            echo "ok    $b"
        else
            echo "FAIL  $b ($work/c/$b.diff)"
            rc=1
        fi
    done
    return $rc
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
host) build_host ;;
expect) expect "$@" ;;
run)
    [ $# -eq 1 ] || die "run <test.c>"
    run_one "$1"
    cat "$work/c/$(basename "$1" .c).out"
    ;;
check) check "$@" ;;
*)
    echo "usage: ncc18.sh {host | expect [<test.c>...] | run <test.c> | check [<test.c>...]}" >&2
    exit 2
    ;;
esac
