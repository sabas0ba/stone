#!/bin/sh
# 生成物のビルド。成果物は tmp/build/ (git ignore) に置く。
# 各 Stage の成果物は前段の成果物のみでビルドする (docs/plan.md 2.1)。
#
# 使用法: build.sh [stage002|...|stage008|stage009|all]
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
build_stage008() {
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/cc0.bin
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/ld0.bin
    echo "built tmp/build/cc0.bin tmp/build/ld0.bin (bootstrap)" >&2
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/cc.o
    { cat tmp/build/cc.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/cc.bin
    echo "built tmp/build/cc.bin" >&2
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/ld.o
    { cat tmp/build/ld.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/ld.bin
    echo "built tmp/build/ld.bin" >&2
}

# Stage 9 は cc + ld でビルドする。pp 自身は指令を含まないため，
# 前処理を通さずに直接コンパイルできる。
build_stage009() {
    { cat stage009/pp.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/pp.o
    { cat tmp/build/pp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp.bin
    echo "built tmp/build/pp.bin" >&2
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
all)
    build_stage002
    build_stage003
    build_stage004
    build_stage005
    build_stage006
    build_stage007
    build_stage008
    build_stage009
    ;;
*)
    echo "usage: build.sh [stage002|stage003|stage004|stage005|stage006|stage007|stage008|stage009|all]" >&2
    exit 2
    ;;
esac
