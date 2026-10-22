# nld1 のリンカ台本の試験 (docs/stage018-ncc.md 7 章)。lds.lds で結合する。
# 番地の期待は lds.expect にあり，この入力と台本から手で数えたものである。

    .text
    .globl _start
_start:
    nop                         # .text 4 バイト
    .section .text.b,"ax"
    nop                         # 4 バイト。SORT で .text.a の後ろ
    .section .text.a,"ax"
    nop
    nop                         # 8 バイト
    .section .rodata,"a"
    .byte 1, 2, 3
    .data
    .balign 8
    .word 0x11111111
    .word prov_sym              # PROVIDE が定義する
    .section .init.x,"aw"
    .word 0x22222222
    .section .discard.me,"a"
    .word 0x33333333            # /DISCARD/ が捨てる
    .section .orphan.data,"aw"
    .word 0x44444444            # 台本に無い書込み可の節 -> 書込み可の最後の出力節の後ろ
    .section .orphan.ro,"a"
    .word 0x55555555            # 台本に無い読出し専用の節 -> .rodata の後ろ
    .bss
    .zero 10
    .section .note.ignored
    .word 1                     # 割り付けない節は出力しない
