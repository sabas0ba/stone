#!/bin/sh
# tcc を**ホストの gcc で**ビルドする。Stage 15 第 5 部の開発道具。
#
# 使用法: tcc.sh [host|riscv64|riscv32|src|clean]
#
#   src       素材を tmp/tcc/src へ写し stage015/tcc/*.patch を当てる
#   host      ホスト向けの tcc を作る (tmp/tcc/build/tcc)
#   riscv64   RV64 向けの交差 tcc を作る (上流のまま。手本と対照用)
#   riscv32   RV32 向けの交差 tcc を作る (我々が足す対象)
#   os        我々の OS の上で走る tcc (tccH) を交差 tcc で作る。
#             第 6 部で「実行環境の不足」と「我々の cc の誤訳」を
#             切り分けるための対照 (下の 3 を見よ)
#   base      patch を当てない素の RV64 tcc を作る (tmp/tcc/base/build)。
#             patch が RV64 の生成コードを変えていないことの対照に使う
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
os)
    # 我々の OS の上で走る tcc (tccH) を，**ホストの交差 tcc で**作る。
    #
    # 第 6 部の切り分けのための対照である。T2 が動かないとき，原因は
    # 「我々の cc の誤訳」か「実行環境 (start.S・libc15・kernel16 の
    # ELF 読み) の不足」のどちらかだが，tccH は前者を取り除いた形なので，
    # tccH が動けば実行環境は正しい，と言い切れる。
    #
    # **これは鎖の成果物ではない。** 使うのは対照としてだけである。
    prepare_build
    [ -x "$bld/riscv32-tcc" ] || make -C "$bld" riscv32-tcc
    [ -f "$bld/tccdefs_.h" ] || make -C "$bld" tccdefs_.h
    inc=stage015/libc/include
    os=$bld/os
    rm -rf "$os"
    mkdir -p "$os/sys"
    # 平らな名前空間を組む。stdarg.h / stddef.h は **tcc 自身のもの**を
    # 使う (我々のものは cc 専用の隠しローカルに依る)
    for h in "$inc"/*.h; do
        case $(basename "$h") in stdarg.h|stddef.h) continue ;; esac
        cp "$h" "$os/"
    done
    cp "$inc/sys/time.h" "$os/sys/"
    cp "$src"/include/*.h "$os/"
    cp stage015/tcc/config-stone.h "$os/config.h"
    cp "$bld/tccdefs_.h" "$os/"
    for f in tcc.c libtcc.c tccpp.c tccgen.c tccdbg.c tccasm.c tccelf.c \
             tccrun.c tcctools.c riscv64-gen.c riscv64-link.c riscv64-asm.c \
             tcc.h libtcc.h elf.h stab.h stab.def dwarf.h tcctok.h \
             riscv64-tok.h; do
        cp "$src/$f" "$os/"
    done
    cp stage015/tccrt/start.S "$os/"
    cp "$src"/lib/libtcc1.c "$src"/lib/riscv32.c "$os/"
    for f in src/string src/ctype src/stdlib src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert; do
        cp "stage015/libc/$f.c" "$os/"
    done
    xtcc=$(pwd)/$bld/riscv32-tcc
    (
        cd "$os"
        set -e
        "$xtcc" -c start.S -o start.o
        # 実行時支援 (libtcc1.a 相当)。riscv32.c は 64 bit の除算・
        # シフト・変換 (stage015/tcc/riscv32.patch が足したもの)
        "$xtcc" -c riscv32.c -o riscv32.o
        "$xtcc" -c libtcc1.c -o libtcc1.o
        for f in string ctype stdlib misc15 sys morecore stdio assert; do
            "$xtcc" -nostdinc -I. -c "$f.c" -o "$f.o"
        done
        "$xtcc" -nostdinc -I. -c tcc.c -o tcc.o
        # kernel16 はユーザ像を UBASE = 0x8600_0000 へ載せる
        "$xtcc" -nostdlib -static -Wl,-Ttext=0x86000000 -o ../tccH \
            start.o tcc.o string.o ctype.o stdlib.o misc15.o sys.o \
            morecore.o stdio.o assert.o riscv32.o libtcc1.o
    ) 2>&1 | grep -v 'warning:\|^In file included' || true
    [ -f "$bld/tccH" ] || { echo "error: tccH ができていない" >&2; exit 1; }
    echo "built $bld/tccH ($(wc -c < "$bld/tccH") バイト)" >&2
    ;;
base)
    if [ ! -d "$ext" ]; then
        echo "error: $ext が無い。'sh tools/fetch.sh tcc' で取得する" >&2
        exit 1
    fi
    if [ ! -f tmp/tcc/base/build/config.mak ]; then
        rm -rf tmp/tcc/base
        mkdir -p tmp/tcc/base/build
        cp -a "$ext" tmp/tcc/base/src
        rm -rf tmp/tcc/base/src/.git
        ( cd tmp/tcc/base/build && ../src/configure >configure.log 2>&1 ) \
            || { cat tmp/tcc/base/build/configure.log >&2; exit 1; }
    fi
    make -C tmp/tcc/base/build cross-riscv64
    ;;
clean)
    rm -rf tmp/tcc
    ;;
*)
    echo "usage: tcc.sh [host|riscv64|riscv32|os|src|base|clean]" >&2
    exit 2
    ;;
esac
