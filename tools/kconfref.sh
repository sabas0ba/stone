#!/bin/sh
# 照合用の上流 kconfig (conf) で Linux の設定を作る (docs/stage019-linux.md 4 章)。
#
#   kconfref.sh flex                          flex 2.6.4 を tmp/kconfref/flex に組む
#   kconfref.sh run <出力> <目標> [変数=値...]  Linux 5.4 の Kbuild で <目標> を O=<出力> に作る
#   kconfref.sh cmp <kconf> <目標> [変数=値...]  上流の conf と <kconf> で <目標> を作って突き合わせる
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

# 突き合わせる生成物。include/config/ の下の記号ごとの .h は一覧を比べる
outputs=".config include/config/auto.conf include/config/auto.conf.cmd include/config/tristate.conf include/generated/autoconf.h"

# Kbuild が conf を呼ぶ形 (環境変数と引数) のまま <kconf> を走らせるため，
# 一度上流の conf で目標を作った後，conf を <kconf> で置き換え，生成物を
# 消して同じ目標をもう一度作る。conf の作り直しは .cmd の命令行と依存の
# 時刻で決まるので，置き換えた後は作り直されない
cmp_target() {
    kconf=$1; target=$2; shift 2
    [ -x "$kconf" ] || die "kconf が無い: $kconf"
    ref="$work/cmp/ref"
    ours="$work/cmp/ours"
    rm -rf "$ref" "$ours"
    run_target "$ref" "$target" "$@" > "$work/cmp/ref.log" 2>&1 \
        || die "上流の conf が失敗した ($work/cmp/ref.log)"
    run_target "$ours" "$target" "$@" > /dev/null 2>&1 \
        || die "上流の conf が失敗した (置き換える前)"
    cp "$kconf" "$ours/scripts/kconfig/conf"
    rm -rf "$ours/.config" "$ours/.config.old" "$ours/include/config" "$ours/include/generated"
    run_target "$ours" "$target" "$@" > "$work/cmp/ours.log" 2>&1 \
        || die "kconf が失敗した ($work/cmp/ours.log)"
    rc=0
    for f in $outputs; do
        if cmp -s "$ref/$f" "$ours/$f"; then
            echo "same    $f"
        else
            echo "differ  $f"
            rc=1
        fi
    done
    (cd "$ref" && find include/config -name '*.h' | LC_ALL=C sort) > "$work/cmp/ref.deps"
    (cd "$ours" && find include/config -name '*.h' | LC_ALL=C sort) > "$work/cmp/ours.deps"
    if cmp -s "$work/cmp/ref.deps" "$work/cmp/ours.deps"; then
        echo "same    include/config/**.h ($(wc -l < "$work/cmp/ref.deps" | tr -d ' ') 個)"
    else
        echo "differ  include/config/**.h"
        rc=1
    fi
    return $rc
}

cmd=${1:-}
case "$cmd" in
flex) build_flex ;;
run)
    [ $# -ge 3 ] || die "run <出力> <目標> [変数=値...]"
    shift
    run_target "$@"
    ;;
cmp)
    [ $# -ge 3 ] || die "cmp <kconf> <目標> [変数=値...]"
    shift
    cmp_target "$@"
    ;;
*)
    echo "usage: kconfref.sh {flex | run <出力> <目標> [変数=値...] | cmp <kconf> <目標> [変数=値...]}" >&2
    exit 2
    ;;
esac
