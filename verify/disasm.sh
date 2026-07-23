#!/bin/sh
# verify 層: フラットバイナリを逆アセンブルし，listing との照合に用いる。
# 読取り専用の検証であり，ビルド成果物には影響しない (docs/plan.md 2.2)。
#
# 使用法: disasm.sh <repo 相対パスの flat.bin>
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

exec sh "$repo_root/tools/env.sh" run \
    riscv64-unknown-elf-objdump -b binary -m riscv:rv32 \
    -M no-aliases,numeric --adjust-vma=0x80000000 -D "$1"
