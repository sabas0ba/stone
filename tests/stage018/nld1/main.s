# nld1 の結合の試験 (docs/stage018-ncc.md 7 章の 2)。other.s と結合し，
# 素の実行環境 (tests/stage018/rt) の上で走らせる。各項目が "ok <名前>" か
# "NG <名前>" を書き，main は NG の数を返す (終了コードになる)。
# 再配置の型ごとに，別の .o にある記号を参照する。

    .text
# report(a0 = 真偽, a1 = 名前)
report:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    mv s0, a1
    bnez a0, 1f
    la t0, fails
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    la a0, s_ng
    j 2f
1:  la a0, s_ok
2:  call puts
    mv a0, s0
    call puts
    li a0, '\n'
    call putchar
    lw ra, 12(sp)
    lw s0, 8(sp)
    addi sp, sp, 16
    ret

    .macro check name
    .pushsection .rodata
1:  .asciz "\name"
    .popsection
    la a1, 1b
    call report
    .endm

    .globl main
main:
    addi sp, sp, -16
    sw ra, 12(sp)

    # CALL_PLT: 別の .o の関数を呼ぶ
    li a0, 21
    call twice
    addi a0, a0, -42
    seqz a0, a0
    check call_plt

    # PCREL_HI20 / PCREL_LO12_I: 別の .o の .data を読む
    la t0, ext_word
    lw t1, 0(t0)
    li t2, 0x12345678
    sub a0, t1, t2
    seqz a0, a0
    check pcrel_load

    # HI20 / LO12_I: 絶対番地で読む
    lui t0, %hi(ext_word)
    lw t1, %lo(ext_word)(t0)
    li t2, 0x12345678
    sub a0, t1, t2
    seqz a0, a0
    check hi20_lo12_i

    # LO12_S: 絶対番地で別の .o の .bss に書き，PC 相対で読み戻す
    lui t0, %hi(ext_bss)
    li t1, 0x5a5a
    sw t1, %lo(ext_bss)(t0)
    la t2, ext_bss
    lw t3, 0(t2)
    sub a0, t3, t1
    seqz a0, a0
    check lo12_s

    # PCREL_LO12_S: PC 相対で書く
3:  auipc t0, %pcrel_hi(ext_bss)
    li t1, 0x77
    sw t1, %pcrel_lo(3b)(t0)
    lui t2, %hi(ext_bss)
    lw t3, %lo(ext_bss)(t2)
    sub a0, t3, t1
    seqz a0, a0
    check pcrel_lo12_s

    # JAL: 別の .o の関数へ jal
    li a0, 5
    jal ra, twice
    addi a0, a0, -10
    seqz a0, a0
    check jal

    # BRANCH: 別の .o のラベルへ分岐し，そこから戻る
    li a0, 0
    beq zero, zero, other_target
    .globl back_from_other
back_from_other:
    check branch

    # R_RISCV_32: .data の関数の表から呼ぶ
    la t0, fntab
    lw t1, 4(t0)
    li a0, 7
    jalr t1
    addi a0, a0, -8
    seqz a0, a0
    check word32

    # ADD32 / SUB32: 別の .o の記号との差
    la t0, diff
    lw t1, 0(t0)
    la t2, ext_word
    la t3, here_label
    sub t2, t2, t3
    sub a0, t1, t2
    seqz a0, a0
    check add32_sub32

    # weak: 自分の weak な定義より other.s の強い定義が勝つ
    call wk
    addi a0, a0, -2
    seqz a0, a0
    check weak_override

    # weak な未定義は 0
    lui t0, %hi(missing)
    addi t0, t0, %lo(missing)
    seqz a0, t0
    check weak_undef

    # COMMON: 両方の .o が持つ cbuf は 1 つにまとまる
    la t0, cbuf
    li t1, 99
    sw t1, 0(t0)
    call read_cbuf
    addi a0, a0, -99
    seqz a0, a0
    check common

    # 台本の記号: __bss_start <= __bss_end <= _end
    la t0, __bss_start
    la t1, __bss_end
    la t2, _end
    sltu a0, t1, t0
    sltu t3, t2, t1
    or a0, a0, t3
    seqz a0, a0
    check script_symbols

    # 別の .o の .rodata の文字列
    la a0, greeting
    call puts
    li a0, 1
    check rodata

    la t0, fails
    lw a0, 0(t0)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

    .weak wk
wk:
    li a0, 1
    ret

    .weak missing

    .data
    .balign 4
fntab:
    .word twice, inc
here_label:
diff:
    .word ext_word - here_label
fails:
    .word 0

    .comm cbuf, 16, 8

    .section .rodata
s_ok:
    .asciz "ok "
s_ng:
    .asciz "NG "
