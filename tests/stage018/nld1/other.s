# nld1 の結合の試験の相手 (main.s から参照される記号を定義する)

    .text
    .globl twice
twice:
    add a0, a0, a0
    ret

    .globl inc
inc:
    addi a0, a0, 1
    ret

    .globl wk
wk:
    li a0, 2
    ret

    .globl read_cbuf
read_cbuf:
    la t0, cbuf
    lw a0, 0(t0)
    ret

# main.s の branch の項目が beq で跳んでくる
    .globl other_target
other_target:
    li a0, 1
    j back_from_other

    .data
    .globl ext_word
    .balign 4
ext_word:
    .word 0x12345678

    .bss
    .globl ext_bss
    .balign 4
ext_bss:
    .zero 8

    .comm cbuf, 16, 8

    .section .rodata
    .globl greeting
greeting:
    .asciz "hello from other.s\n"
