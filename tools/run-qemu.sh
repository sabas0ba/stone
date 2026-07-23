#!/bin/sh
# QEMU virt (RV32) 上でフラットバイナリをフィルタとして実行する。
#
# 使用法: run-qemu.sh <flat.bin> [追加の QEMU オプション...]
#   - <flat.bin> は RAM 先頭 0x8000_0000 に配置され，そこから実行される
#   - stdin  -> UART RX (0x1000_0000)
#   - stdout <- UART TX
#   - test finisher (0x0010_0000) への書込みで終了コード付きで停止する
#
# 環境変数 (いずれも観測専用であり，成果物の出力には影響しない):
#   STONE_QEMU_TRACE  実行トレースの記録先ファイルパス (コンテナ内パス。/work 配下を推奨)。
#                     翻訳ブロックを命令単位に分割し (-singlestep)，実行命令と
#                     割込み・例外を記録する (-d in_asm,int)。
#                     レジスタ値も必要な場合は追加オプションで -d in_asm,cpu,int を渡す
#   STONE_QEMU_GDB    GDB stub の待受け TCP ポート。指定時は最初の命令を実行する前に
#                     停止し (-S)，デバッガの接続を待つ
set -eu

bios="$1"
shift

set -- \
    -M virt \
    -bios "$bios" \
    -display none \
    -monitor none \
    -serial stdio \
    "$@"

if [ -n "${STONE_QEMU_TRACE:-}" ]; then
    set -- "$@" -singlestep -d in_asm,int -D "$STONE_QEMU_TRACE"
fi

if [ -n "${STONE_QEMU_GDB:-}" ]; then
    set -- "$@" -S -gdb "tcp::${STONE_QEMU_GDB}"
fi

exec qemu-system-riscv32 "$@"
