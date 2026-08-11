#!/bin/sh
# tcc を**ホストの gcc で**ビルドする。Stage 15 第 5 部の開発道具。
#
# 使用法: tcc.sh [host|riscv64|riscv32|src|clean]
#
#   src       素材を tmp/tcc/src へ写し stage015/tcc/*.patch を当てる
#   host      ホスト向けの tcc を作る (tmp/tcc/build/tcc)
#   riscv64   RV64 向けの交差 tcc を作る (上流のまま。手本と対照用)
#   riscv32   RV32 向けの交差 tcc を作る (我々が足す対象)
#   clean     tmp/tcc を消す
#
# riscv32 は**コンパイラ本体だけ**を作る。実行時支援 (riscv32-libtcc1.a)
# は呼出し規約が入るまで作れない (docs/stage015-riscv32.md 6 章 その 3)。
#
# **これはブートストラップ鎖ではない。** 鎖の掟 (docs/plan.md 2.1
# 「各段は前段の成果物のみから作る」) はここには掛からない。ここで作る
# tcc はホストの gcc が作った実行形式であり，鎖の入力には一切使わない。
#
# 用途は 2 つ。
#
#   1. `riscv32-gen.c` 相当の改訂 (stage015/tcc/riscv32.patch) を書くとき，
#      ホストで作った交差 tcc に RV32 のコードを吐かせて，我々の QEMU と
#      OS の上で走らせて確かめる。処理系の側 (第 2〜4 部) の進み具合に
#      関係なく進められる
#   2. 第 6 部で我々の処理系が作った tcc (T1) と突き合わせるときの，
#      「正しい tcc はこう振る舞う」の基準
#
# 素材は docs/external/tcc に要る (無ければ tools/fetch.sh tcc)。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

ext=docs/external/tcc
src=tmp/tcc/src
bld=tmp/tcc/build

prepare_src() {
    if [ ! -d "$ext" ]; then
        echo "error: $ext が無い。'sh tools/fetch.sh tcc' で取得する" >&2
        exit 1
    fi
    rm -rf "$src"
    mkdir -p tmp/tcc
    cp -a "$ext" "$src"
    rm -rf "$src/.git"
    for p in stage015/tcc/*.patch; do
        [ -e "$p" ] || continue
        echo "apply $p" >&2
        patch -s -p1 -d "$src" < "$p"
    done
}

prepare_build() {
    [ -d "$src" ] || prepare_src
    if [ ! -f "$bld/config.mak" ]; then
        mkdir -p "$bld"
        ( cd "$bld" && ../src/configure >configure.log 2>&1 ) \
            || { cat "$bld/configure.log" >&2; exit 1; }
    fi
}

cmd=${1:-host}
case "$cmd" in
src)
    prepare_src
    ;;
host)
    prepare_build
    make -C "$bld"
    ;;
riscv64)
    prepare_build
    make -C "$bld" cross-riscv64
    ;;
riscv32)
    prepare_build
    make -C "$bld" riscv32-tcc
    ;;
clean)
    rm -rf tmp/tcc
    ;;
*)
    echo "usage: tcc.sh [host|riscv64|riscv32|src|clean]" >&2
    exit 2
    ;;
esac
