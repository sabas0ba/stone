#!/bin/sh
# 生成物のビルド。成果物は tmp/build/ (git ignore) に置く。
# 各 Stage の成果物は前段の成果物のみでビルドする (docs/plan.md 2.1)。
#
# 使用法: build.sh [stage002|...|stage009|stage010|all]
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
mkdir -p tmp/build

build_stage002() {
    sh tools/env.sh qemu stage001/hex0.bin < stage002/hex1.hex > tmp/build/hex1.bin
    echo "built tmp/build/hex1.bin" >&2
}

build_stage003() {
    sh tools/env.sh qemu tmp/build/hex1.bin < stage003/asm.hex1 > tmp/build/asm.bin
    echo "built tmp/build/asm.bin" >&2
}

build_stage004() {
    sh tools/env.sh qemu tmp/build/asm.bin < stage004/sol.s > tmp/build/sol.bin
    echo "built tmp/build/sol.bin" >&2
}

build_stage005() {
    sh tools/env.sh qemu tmp/build/sol.bin < stage005/sc.sol > tmp/build/sc.bin
    echo "built tmp/build/sc.bin" >&2
}

# sc 入力の終端は EOT (0x04)
build_stage006() {
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/sc.bin > tmp/build/scc1.bin
    echo "built tmp/build/scc1.bin" >&2
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc1.bin > tmp/build/scc.bin
    echo "built tmp/build/scc.bin" >&2
}

build_stage007() {
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc.bin > tmp/build/occ1.bin
    echo "built tmp/build/occ1.bin" >&2
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ1.bin > tmp/build/occ.bin
    echo "built tmp/build/occ.bin" >&2
}

# Stage 8 以降は「コンパイル -> リンク」の 2 段になる。
# cc / ld 自身のブートストラップは occ (フラット出力) が担う。
# 生成物を cc8.bin と呼ぶのは，Stage 10 で後継の C コンパイラが出てくるため。
# cc.bin は常に最新世代を指し，過去世代は世代番号を付けて呼ぶ
# (docs/stage010-c89.md 2.1)。
build_stage008() {
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/cc0.bin
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/ld0.bin
    echo "built tmp/build/cc0.bin tmp/build/ld0.bin (bootstrap)" >&2
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/cc8.o
    { cat tmp/build/cc8.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/cc8.bin
    echo "built tmp/build/cc8.bin" >&2
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/ld.o
    { cat tmp/build/ld.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/ld.bin
    echo "built tmp/build/ld.bin" >&2
}

# Stage 9 は Stage 8 の cc + ld でビルドする。pp 自身は指令を含まないため，
# 前処理を通さずに直接コンパイルできる。前段の成果物のみでビルドするという
# 約束のとおり，ここは Stage 10 の cc ではなく cc8 を使う。
build_stage009() {
    { cat stage009/pp.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc8.bin > tmp/build/pp.o
    { cat tmp/build/pp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp.bin
    echo "built tmp/build/pp.bin" >&2
}

# Stage 10 は 3 部に分かれ，各部のコンパイラが次の部をビルドする。
# 生成物は世代ごとに名前を持ち (cc10a = 第 1 部, cc10b = 第 2 部)，
# 最新世代を cc.bin として複製する (docs/stage010-c89.md 2.1)。
#
#   cc8   -> cc10a0 -> cc10a  (第 1 部。cc10a0 == cc10a)
#   cc10a -> cc10b           (第 2 部)
build_stage010() {
    { cat stage010/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc8.bin > tmp/build/cc10a0.o
    { cat tmp/build/cc10a0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10a0.bin
    echo "built tmp/build/cc10a0.bin (bootstrap)" >&2
    { cat stage010/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10a0.bin > tmp/build/cc10a.o
    { cat tmp/build/cc10a.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10a.bin
    echo "built tmp/build/cc10a.bin" >&2
    { cat stage010/cc2.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10a.bin > tmp/build/cc10b.o
    { cat tmp/build/cc10b.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10b.bin
    echo "built tmp/build/cc10b.bin" >&2
    # 第 2 部の 2 はフレームの割付け方を変えるので，1 段目 (cc10b が作ったもの)
    # と正本は一致しない。正本はその 1 段目が自分自身を再コンパイルしたもので，
    # 以降は固定点になる (B2 == B3)
    { cat stage010/cc3.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10b.bin > tmp/build/cc10c0.o
    { cat tmp/build/cc10c0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10c0.bin
    echo "built tmp/build/cc10c0.bin (bootstrap)" >&2
    { cat stage010/cc3.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10c0.bin > tmp/build/cc10c.o
    { cat tmp/build/cc10c.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10c.bin
    echo "built tmp/build/cc10c.bin" >&2
    # cc.bin は常に最新世代を指す別名
    cp tmp/build/cc10c.bin tmp/build/cc.bin
    echo "built tmp/build/cc.bin (= cc10c.bin)" >&2
}

case "${1:-all}" in
stage002)
    build_stage002
    ;;
stage003)
    build_stage002
    build_stage003
    ;;
stage004)
    build_stage002
    build_stage003
    build_stage004
    ;;
stage005)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    ;;
stage006)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    ;;
stage007)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    ;;
stage008)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    build_stage008
    ;;
stage009)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    build_stage008
    build_stage009
    ;;
stage010)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    build_stage008
    build_stage009
    build_stage010
    ;;
all)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    build_stage008
    build_stage009
    build_stage010
    ;;
*)
    echo "usage: build.sh [stage002|...|stage009|stage010|all]" >&2
    exit 2
    ;;
esac
