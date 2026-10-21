# 素の実行環境の出力 (docs/stage018-ncc.md 7 章の 2)。UART (16550，0x1000_0000)
# に 1 文字ずつ書く。呼出し規約は psABI (引数は a0，ra に戻る)。

    .text
# putchar(a0): LSR (位置 5) の THRE (bit 5) が立つのを待ってから THR に書く
    .globl putchar
putchar:
    li t0, 0x10000000
1:  lbu t1, 5(t0)
    andi t1, t1, 0x20
    beqz t1, 1b
    sb a0, 0(t0)
    ret

# puts(a0): 0 で終わる文字列を書く (改行は足さない)
    .globl puts
puts:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    mv s0, a0
1:  lbu a0, 0(s0)
    beqz a0, 2f
    call putchar
    addi s0, s0, 1
    j 1b
2:  lw ra, 12(sp)
    lw s0, 8(sp)
    addi sp, sp, 16
    ret

# puthex(a0): 8 桁の 16 進で書く
    .globl puthex
puthex:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    mv s0, a0
    li s1, 28
1:  srl a0, s0, s1
    andi a0, a0, 15
    li t0, 10
    blt a0, t0, 2f
    addi a0, a0, 'a' - 10 - '0'
2:  addi a0, a0, '0'
    call putchar
    addi s1, s1, -4
    bgez s1, 1b
    lw ra, 12(sp)
    lw s0, 8(sp)
    lw s1, 4(sp)
    addi sp, sp, 16
    ret
