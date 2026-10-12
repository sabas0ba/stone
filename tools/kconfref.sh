#!/bin/sh
# 照合用の上流 kconfig (conf) で Linux の設定を作る (docs/stage019-linux.md 4 章)。
#
#   kconfref.sh flex                          flex 2.6.4 を tmp/kconfref/flex に組む
#   kconfref.sh run <出力> <目標> [変数=値...]  Linux 5.4 の Kbuild で <目標> を O=<出力> に作る
#
# 我々の kconf (Kconfig を読む道具) が出す .config と生成物を，上流の conf の
# 出力と突き合わせるために使う。**stone のビルドチェーンにもカーネルにも入らない。**
# 上流の conf は字句解析器を flex で，構文解析器を bison で作る。flex は
# tools/fetch.sh flex で取った 2.6.4 を tmp の中で組み，install しない。bison は
# ホストのものを使う (docs/SOURCES.md)。
#
# ARCH は riscv に固定する。<変数=値> は make にそのまま渡す (CC=... など)。
# Kconfig は $(CC) や $(LD) を実行して値を決める (scripts/Kconfig.include) ので，
# 突き合わせる両者には同じ CC / LD を渡すこと。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work="$repo_root/tmp/kconfref"
linux="$repo_root/docs/external/linux54"
flexsrc="$repo_root/docs/external/flex"
flexbin="$work/flex/src/flex"

die() { echo "kconfref.sh: $*" >&2; exit 1; }

build_flex() {
    [ -f "$flexsrc/configure" ] || die "flex のソースが無い (sh tools/fetch.sh flex)"
    rm -rf "$work/flex"
    mkdir -p "$work/flex"
    # 字句解析器の生成に要るのは src/flex だけである。文書 (help2man) と
    # 多言語化は組まない
    (cd "$work/flex" \
        && "$flexsrc/configure" --disable-nls --disable-shared > configure.log 2>&1 \
        && make -C src flex > make.log 2>&1) \
        || die "flex を組めない ($work/flex の configure.log / make.log を見る)"
    "$flexbin" --version
}

run_target() {
    out=$1; target=$2; shift 2
    [ -x "$flexbin" ] || die "flex が無い (sh tools/kconfref.sh flex)"
    [ -f "$linux/Makefile" ] || die "Linux のソースが無い (sh tools/fetch.sh linux54)"
    mkdir -p "$out"
    make -s -C "$linux" O="$out" ARCH=riscv LEX="$flexbin" YACC=bison "$@" "$target"
}

cmd=${1:-}
case "$cmd" in
flex) build_flex ;;
run)
    [ $# -ge 3 ] || die "run <出力> <目標> [変数=値...]"
    shift
    run_target "$@"
    ;;
*)
    echo "usage: kconfref.sh {flex | run <出力> <目標> [変数=値...]}" >&2
    exit 2
    ;;
esac
