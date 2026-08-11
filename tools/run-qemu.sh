#!/bin/sh
# QEMU virt (RV32) 上でフラットバイナリをフィルタとして実行する。
#
# 使用法: run-qemu.sh <flat.bin | prog.elf> [追加の QEMU オプション...]
#   - <flat.bin> は RAM 先頭 0x8000_0000 に配置され，そこから実行される
#   - ELF を渡すと program header どおりに読み込み entry から始める
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
#   STONE_QEMU_RAMFILE
#                     RAM (128 MiB) 全体を裏づけるホスト側ファイル (share=on)。
#                     ゲストの書込みがそのままファイルへ現れるので，共有領域
#                     (sfs イメージ) の注入と回収に使う (docs/stage012-os.md 4 章)。
#                     こちらは観測専用ではなく，ファイル交換の正規の経路である
set -eu

bios="$1"
shift

# 与えられたのが ELF なら，program header どおりに読み込んで entry から
# 始める (-device loader)。平らな像は従来どおり -bios で 0x8000_0000 へ置く。
#
# 鎖の成果物はすべて平らな像なので，この枝を通るのは第 5 部で tcc に
# 吐かせたものだけである (docs/stage015-riscv32.md)。tcc の出す ELF は
# 節ごとに 0x1000 境界へ揃うため，そのまま平らに落とすと番地が合わない。
if [ "$(od -An -c -N 4 "$bios" | tr -d ' ')" = '177ELF' ]; then
    set -- \
        -M virt \
        -bios none \
        -device "loader,file=$bios,cpu-num=0" \
        -display none \
        -monitor none \
        -serial stdio \
        "$@"
else
    set -- \
        -M virt \
        -bios "$bios" \
        -display none \
        -monitor none \
        -serial stdio \
        "$@"
fi

if [ -n "${STONE_QEMU_RAMFILE:-}" ]; then
    set -- "$@" -m 128M \
        -object memory-backend-file,id=stone-ram,size=128M,mem-path="$STONE_QEMU_RAMFILE",share=on \
        -machine memory-backend=stone-ram
fi

if [ -n "${STONE_QEMU_TRACE:-}" ]; then
    set -- "$@" -singlestep -d in_asm,int -D "$STONE_QEMU_TRACE"
fi

if [ -n "${STONE_QEMU_GDB:-}" ]; then
    set -- "$@" -S -gdb "tcp::${STONE_QEMU_GDB}"
fi

exec qemu-system-riscv32 "$@"
