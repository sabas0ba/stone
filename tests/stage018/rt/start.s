# 素の実行環境の起動 (docs/stage018-ncc.md 7 章の 2)。
#
# QEMU virt (-bios none) が ELF を読み込み，入口 _start から走らせる。
# スタックを用意し，.bss を 0 で埋め，main を呼び，その戻り値を終了コード
# にして exit で止める。番地は nld1 の既定の台本が定義する記号
# (__stack_top，__bss_start，__bss_end) を使う。

    .section .text.init,"ax"
    .globl _start
_start:
    la sp, __stack_top
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sb zero, 0(t0)
    addi t0, t0, 1
    j 1b
2:  call main
    tail exit

# exit(a0): test finisher (0x0010_0000) に書いて QEMU を止める。
# 0 なら 0x5555 (終了コード 0)，それ以外は (code << 16) | 0x3333
    .text
    .globl exit
exit:
    li t0, 0x100000
    beqz a0, 1f
    slli a0, a0, 16
    li t1, 0x3333
    or a0, a0, t1
    sw a0, 0(t0)
2:  j 2b
1:  li t1, 0x5555
    sw t1, 0(t0)
    j 2b
