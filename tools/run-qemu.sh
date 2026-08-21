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
#                     RAM 全体を裏づけるホスト側ファイル (share=on)。
#                     ゲストの書込みがそのままファイルへ現れるので，共有領域
#                     (sfs イメージ) の注入と回収に使う (docs/stage012-os.md 4 章)。
#                     こちらは観測専用ではなく，ファイル交換の正規の経路である。
#                     疎ファイルでよい (触れたページだけが実際に消費される)
#   STONE_QEMU_RAM    RAM 量 (既定 128M)。RAMFILE を使うときだけ効く。
#                     既定を変えないのは，凍結済みの世代を当時と同じ大きさで
#                     走らせ続けるためである (docs/stage016-os.md 8 章)
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
    # RAM 量は既定 128M。STONE_QEMU_RAM で変えられる
    # (Stage 16 第 3 部。docs/stage016-os.md 8 章)。
    #
    # **既定を変えないのは，凍結済みの世代をそのままの環境で走らせ続ける
    # ためである。** kernel15〜18 は 128 MB を前提にした配置を持っており，
    # それらの検査は当時と同じ大きさで再現できるほうがよい。広い記憶域を
    # 要るのは kernel19 以降だけなので，要る側が明示的に指定する。
    #
    # RAMFILE は疎ファイルでよい。QEMU が触れたページだけが実際に
    # ディスクを消費する
    _ram=${STONE_QEMU_RAM:-128M}
    set -- "$@" -m "$_ram" \
        -object memory-backend-file,id=stone-ram,size="$_ram",mem-path="$STONE_QEMU_RAMFILE",share=on \
        -machine memory-backend=stone-ram
fi

if [ -n "${STONE_QEMU_TRACE:-}" ]; then
    set -- "$@" -singlestep -d in_asm,int -D "$STONE_QEMU_TRACE"
fi

if [ -n "${STONE_QEMU_GDB:-}" ]; then
    set -- "$@" -S -gdb "tcp::${STONE_QEMU_GDB}"
fi

exec qemu-system-riscv32 "$@"
