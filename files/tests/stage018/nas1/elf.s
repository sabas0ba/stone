# nas1 の節・データ・再配置・記号の試験 (docs/stage018-ncc.md 7 章の 1)。
# 期待は elf.expect にあり，番地と値はこの入力から手で数えたものである。
# 照合は verify/nas1elf.sh の出力との差分で行う。

    .text
    .globl start
    .type start, @function
start:                          # .text 0x00
    la a0, dvar                 # 0x00 PCREL_HI20 dvar / 0x04 PCREL_LO12_I .Lpcrel_hi0
    call extfn                  # 0x08 CALL_PLT extfn
    lui a1, %hi(extvar)         # 0x10 HI20 extvar
    addi a1, a1, %lo(extvar)    # 0x14 LO12_I extvar
    sw a2, %lo(extvar+8)(a1)    # 0x18 LO12_S extvar+8
1:  auipc a3, %pcrel_hi(lvar)   # 0x1c PCREL_HI20 .data+8 (lvar は局所)
    lw a3, %pcrel_lo(1b)(a3)    # 0x20 PCREL_LO12_I .Ln1_0
    beq a0, a1, extfn           # 0x24 BRANCH extfn
    jal extfn                   # 0x28 JAL extfn
    beq a0, a1, wk              # 0x2c BRANCH wk (weak は同じ節でも解かない)
    j start                     # 0x30 その場で解く
    .weak wk
wk: ret                         # 0x34
    .size start, . - start      # 0x38
    tail extfn                  # 0x38 CALL_PLT extfn
end_text:                       # 0x40

    .data
    .globl dvar
    .type dvar, @object
dvar:
    .word 0x11223344            # 0x00
    .half 0x5566                # 0x04
    .byte 1, 2                  # 0x06
lvar:
    .word extvar + 4            # 0x08 R_32 extvar+4
    .word start                 # 0x0c R_32 start
    .word end_text              # 0x10 R_32 .text+0x40
    .dword 0x0102030405060708   # 0x14
    .dword _end - start         # 0x1c ADD64 _end / SUB64 start
    .word extvar - lvar         # 0x24 ADD32 extvar / SUB32 .data+8
    .word end_text - start      # 0x28 同じ節の差は定数 0x40
    .ascii "ab"                 # 0x2c
    .asciz "c\n"                # 0x2e
    .balign 4, 0xee             # 0x31 から 0x34 まで 0xee
    .zero 2                     # 0x34
    .fill 2, 2, 0x1234          # 0x36
    .size dvar, . - dvar        # 0x3a

    .bss
    .globl bvar
bvar:
    .zero 16

    .comm cvar, 8, 4

    .globl alias1
    .set alias1, start + 8
    .globl hid
    .hidden hid
    .set hid, dvar

    .section .init.text,"ax"
init1:
    nop
    .pushsection __ex_table,"a"
    .balign 4
    .word init1, start          # R_32 .init.text+0 / R_32 start
    .popsection
    nop
    .section .rodata.str1.1,"aMS",@progbits,1
    .asciz "x"
    .previous
    nop
    .section .bss.big,"aw",@nobits
    .zero 32
    .section .note.test,"a"
    .weak wext
    .word wext                  # R_32 wext
