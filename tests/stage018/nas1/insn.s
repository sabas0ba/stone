# nas1 の符号化の試験 (docs/stage018-ncc.md 7 章の 1)。
#
# 各行の "# expect:" の後ろが，verify 層の objdump (-M no-aliases,numeric) が
# 読むはずの命令である。期待は ISA の文面から書いた正規形で，疑似命令が
# 複数の命令になるものは " | " で区切る。分岐と jal の行き先は命令からの
# 相対 (.+N / .-N) で書く (試験の側で objdump の絶対番地を直す)。
# 照合は tests/stage018/test.sh が行う。

    .text
start:
# RV32I: 演算
    add a0, a1, a2              # expect: add x10,x11,x12
    sub a0, a1, a2              # expect: sub x10,x11,x12
    sll t0, t1, t2              # expect: sll x5,x6,x7
    slt s0, s1, s2              # expect: slt x8,x9,x18
    sltu s3, s4, s5             # expect: sltu x19,x20,x21
    xor s6, s7, s8              # expect: xor x22,x23,x24
    srl s9, s10, s11            # expect: srl x25,x26,x27
    sra t3, t4, t5              # expect: sra x28,x29,x30
    or t6, zero, ra             # expect: or x31,x0,x1
    and sp, gp, tp              # expect: and x2,x3,x4
    addi a0, a1, -5             # expect: addi x10,x11,-5
    addi a0, a1, 2047           # expect: addi x10,x11,2047
    addi a0, a1, -2048          # expect: addi x10,x11,-2048
    slti a2, a3, 7              # expect: slti x12,x13,7
    sltiu a4, a5, 1             # expect: sltiu x14,x15,1
    xori a6, a7, -1             # expect: xori x16,x17,-1
    ori a0, a0, 0x7ff           # expect: ori x10,x10,2047
    andi a0, a0, 255            # expect: andi x10,x10,255
    slli a0, a0, 31             # expect: slli x10,x10,0x1f
    srli a0, a0, 1              # expect: srli x10,x10,0x1
    srai a0, a0, 3              # expect: srai x10,x10,0x3
    lui t0, 0x12345             # expect: lui x5,0x12345
    lui t0, 0xfffff             # expect: lui x5,0xfffff
    auipc t1, 1                 # expect: auipc x6,0x1
# 即値の形の R 命令 (GNU as と同じく I 形にする)
    add a3, a3, 4               # expect: addi x13,x13,4
    and a0, a0, 7               # expect: andi x10,x10,7
    or a0, a0, 8                # expect: ori x10,x10,8
    xor a0, a0, 1               # expect: xori x10,x10,1
    sll a0, a0, 2               # expect: slli x10,x10,0x2
    srl a0, a0, 2               # expect: srli x10,x10,0x2
    sra a0, a0, 2               # expect: srai x10,x10,0x2
    slt a0, a1, 3               # expect: slti x10,x11,3
    sltu a0, a1, 3              # expect: sltiu x10,x11,3
# レジスタの名前
    addi fp, s1, 0              # expect: addi x8,x9,0
    addi x31, x0, 1             # expect: addi x31,x0,1
    ADDI a0, a0, 1              # expect: addi x10,x10,1
# 読み書き
    lb a0, 0(a1)                # expect: lb x10,0(x11)
    lh a0, -2(a1)               # expect: lh x10,-2(x11)
    lw a0, 2047(sp)             # expect: lw x10,2047(x2)
    lbu a0, -2048(sp)           # expect: lbu x10,-2048(x2)
    lhu a0, 6(s0)               # expect: lhu x10,6(x8)
    lw a0, (a1)                 # expect: lw x10,0(x11)
    sb a0, -1(sp)               # expect: sb x10,-1(x2)
    sh a1, 2(sp)                # expect: sh x11,2(x2)
    sw ra, 12(sp)               # expect: sw x1,12(x2)
    sw a0, (a1)                 # expect: sw x10,0(x11)
# %hi / %lo に定数を与えた形 (その場で解く)
    lui a0, %hi(0x12345678)     # expect: lui x10,0x12345
    addi a0, a0, %lo(0x12345678)    # expect: addi x10,x10,1656
    lui a0, %hi(0x12345fff)     # expect: lui x10,0x12346
    lw a0, %lo(0x12345fff)(a0)  # expect: lw x10,-1(x10)
    sw a0, %lo(0x800)(a1)       # expect: sw x10,-2048(x11)
# 分岐 (同じ節の中はその場で解く)
    beq a0, a1, .+8             # expect: beq x10,x11,.+8
    bne a0, a1, .-4             # expect: bne x10,x11,.-4
    blt a0, a1, .+4094          # expect: blt x10,x11,.+4094
    bge a0, a1, .-4096          # expect: bge x10,x11,.-4096
    bltu a0, a1, .+16           # expect: bltu x10,x11,.+16
    bgeu a0, a1, .+0            # expect: bgeu x10,x11,.+0
    beqz a0, .+8                # expect: beq x10,x0,.+8
    bnez a0, .+8                # expect: bne x10,x0,.+8
    blez a0, .+8                # expect: bge x0,x10,.+8
    bgez a0, .+8                # expect: bge x10,x0,.+8
    bltz a0, .+8                # expect: blt x10,x0,.+8
    bgtz a0, .+8                # expect: blt x0,x10,.+8
    bgt a0, a1, .+8             # expect: blt x11,x10,.+8
    ble a0, a1, .+8             # expect: bge x11,x10,.+8
    bgtu a0, a1, .+8            # expect: bltu x11,x10,.+8
    bleu a0, a1, .+8            # expect: bgeu x11,x10,.+8
# 跳躍
    j .+16                      # expect: jal x0,.+16
    jal .+8                     # expect: jal x1,.+8
    jal a0, .-4                 # expect: jal x10,.-4
    jal ra, .+1048574           # expect: jal x1,.+1048574
    j .-1048576                 # expect: jal x0,.-1048576
    jr a0                       # expect: jalr x0,0(x10)
    jr 4(a0)                    # expect: jalr x0,4(x10)
    jalr a0                     # expect: jalr x1,0(x10)
    jalr a1, a0                 # expect: jalr x11,0(x10)
    jalr a1, 8(a0)              # expect: jalr x11,8(x10)
    jalr a1, a0, 12             # expect: jalr x11,12(x10)
    ret                         # expect: jalr x0,0(x1)
# 数字のラベル
1:  j 1b                        # expect: jal x0,.+0
    j 1f                        # expect: jal x0,.+8
    nop                         # expect: addi x0,x0,0
1:  beqz a0, 1b                 # expect: beq x10,x0,.+0
# M
    mul a0, a1, a2              # expect: mul x10,x11,x12
    mulh a0, a1, a2             # expect: mulh x10,x11,x12
    mulhsu a0, a1, a2           # expect: mulhsu x10,x11,x12
    mulhu a0, a1, a2            # expect: mulhu x10,x11,x12
    div a0, a1, a2              # expect: div x10,x11,x12
    divu a0, a1, a2             # expect: divu x10,x11,x12
    rem a0, a1, a2              # expect: rem x10,x11,x12
    remu a0, a1, a2             # expect: remu x10,x11,x12
# A
    lr.w a0, (a1)               # expect: lr.w x10,(x11)
    lr.w.aq a0, (a1)            # expect: lr.w.aq x10,(x11)
    sc.w a0, a1, (a2)           # expect: sc.w x10,x11,(x12)
    sc.w.rl a0, a1, (a2)        # expect: sc.w.rl x10,x11,(x12)
    sc.w x0, a2, 0(sp)          # expect: sc.w x0,x12,(x2)
    amoswap.w a0, a1, (a2)      # expect: amoswap.w x10,x11,(x12)
    amoadd.w a0, a1, (a2)       # expect: amoadd.w x10,x11,(x12)
    amoxor.w a0, a1, (a2)       # expect: amoxor.w x10,x11,(x12)
    amoand.w a0, a1, (a2)       # expect: amoand.w x10,x11,(x12)
    amoor.w a0, a1, (a2)        # expect: amoor.w x10,x11,(x12)
    amomin.w a0, a1, (a2)       # expect: amomin.w x10,x11,(x12)
    amomax.w a0, a1, (a2)       # expect: amomax.w x10,x11,(x12)
    amominu.w a0, a1, (a2)      # expect: amominu.w x10,x11,(x12)
    amomaxu.w a0, a1, (a2)      # expect: amomaxu.w x10,x11,(x12)
    amoswap.w.aq a0, a1, (a2)   # expect: amoswap.w.aq x10,x11,(x12)
    amoadd.w.rl a0, a1, (a2)    # expect: amoadd.w.rl x10,x11,(x12)
    amoor.w.aqrl a0, a1, (a2)   # expect: amoor.w.aqrl x10,x11,(x12)
# Zicsr
    csrrw a0, 0x105, a1         # expect: csrrw x10,stvec,x11
    csrrs a0, sstatus, a1       # expect: csrrs x10,sstatus,x11
    csrrc a0, sie, a1           # expect: csrrc x10,sie,x11
    csrrwi a0, sscratch, 5      # expect: csrrwi x10,sscratch,5
    csrrsi zero, 0x100, 2       # expect: csrrsi x0,sstatus,2
    csrrci zero, sip, 31        # expect: csrrci x0,sip,31
    csrrw a0, satp, a1          # expect: csrrw x10,satp,x11
    csrrs a0, mhartid, x0       # expect: csrrs x10,mhartid,x0
    csrrs a0, 0x7c0, x0         # expect: csrrs x10,0x7c0,x0
    csrr a0, sepc               # expect: csrrs x10,sepc,x0
    csrw sscratch, a0           # expect: csrrw x0,sscratch,x10
    csrs sie, a0                # expect: csrrs x0,sie,x10
    csrc sip, a0                # expect: csrrc x0,sip,x10
    csrwi stvec, 5              # expect: csrrwi x0,stvec,5
    csrsi sstatus, 2            # expect: csrrsi x0,sstatus,2
    csrci sstatus, 2            # expect: csrrci x0,sstatus,2
    csrs 0x100, 2               # expect: csrrsi x0,sstatus,2
    csrw scause, 0              # expect: csrrwi x0,scause,0
    csrrw tp, 0x140, tp         # expect: csrrw x4,sscratch,x4
    rdcycle a0                  # expect: csrrs x10,cycle,x0
    rdtime a0                   # expect: csrrs x10,time,x0
    rdinstret a0                # expect: csrrs x10,instret,x0
    rdcycleh a0                 # expect: csrrs x10,cycleh,x0
    rdtimeh a0                  # expect: csrrs x10,timeh,x0
# 特権・同期
    ecall                       # expect: ecall
    scall                       # expect: ecall
    ebreak                      # expect: ebreak
    sbreak                      # expect: ebreak
    sret                        # expect: sret
    mret                        # expect: mret
    wfi                         # expect: wfi
    fence.i                     # expect: fence.i
    fence                       # expect: fence iorw,iorw
    fence rw, w                 # expect: fence rw,w
    fence i, o                  # expect: fence i,o
    sfence.vma a0, a1           # expect: sfence.vma x10,x11
    sfence.vma a0               # expect: sfence.vma x10,x0
# 疑似命令
    nop                         # expect: addi x0,x0,0
    mv a0, a1                   # expect: addi x10,x11,0
    move a0, a1                 # expect: addi x10,x11,0
    not a0, a1                  # expect: xori x10,x11,-1
    neg a0, a1                  # expect: sub x10,x0,x11
    seqz a0, a1                 # expect: sltiu x10,x11,1
    snez a0, a1                 # expect: sltu x10,x0,x11
    sltz a0, a1                 # expect: slt x10,x11,x0
    sgtz a0, a1                 # expect: slt x10,x0,x11
    sgt a0, a1, a2              # expect: slt x10,x12,x11
    sgtu a0, a1, a2             # expect: sltu x10,x12,x11
    zext.b a0, a1               # expect: andi x10,x11,255
    li a0, 5                    # expect: addi x10,x0,5
    li a0, -2048                # expect: addi x10,x0,-2048
    li a0, 2047                 # expect: addi x10,x0,2047
    li a0, 2048                 # expect: lui x10,0x1 | addi x10,x10,-2048
    li a0, 0x12345678           # expect: lui x10,0x12345 | addi x10,x10,1656
    li a0, 0xfffff800           # expect: addi x10,x0,-2048
    li a0, 0x80000000           # expect: lui x10,0x80000 | addi x10,x10,0
    li a0, 0x7ffff800           # expect: lui x10,0x80000 | addi x10,x10,-2048
    li a0, -1                   # expect: addi x10,x0,-1
    li t6, 0x00040000           # expect: lui x31,0x40 | addi x31,x31,0
    li a0, LATER                # expect: lui x10,0x0 | addi x10,x10,3
    la a0, start                # expect: auipc x10,0x0 | addi x10,x10,0
    lla a1, start               # expect: auipc x11,0x0 | addi x11,x11,0
    lw a2, start                # expect: auipc x12,0x0 | lw x12,0(x12)
    sw a2, start, t0            # expect: auipc x5,0x0 | sw x12,0(x5)
    call start                  # expect: auipc x1,0x0 | jalr x1,0(x1)
    call t0, start              # expect: auipc x5,0x0 | jalr x5,0(x5)
    tail start                  # expect: auipc x6,0x0 | jalr x0,0(x6)
# 式
    addi a0, a0, (1 << 4) | 3   # expect: addi x10,x10,19
    addi a0, a0, 10 - 2 * 3     # expect: addi x10,x10,4
    addi a0, a0, 1 + 2 & 3      # expect: addi x10,x10,3
    addi a0, a0, 'A'            # expect: addi x10,x10,65
    addi sp, sp, -(0)           # expect: addi x2,x2,0
    addi a0, a0, 0b101          # expect: addi x10,x10,5
    addi a0, a0, 017            # expect: addi x10,x10,15
    addi a0, a0, ~0             # expect: addi x10,x10,-1
    addi a0, a0, 7 / 2 + 7 % 2  # expect: addi x10,x10,4
    addi a0, a0, (end - start) & 0  # expect: addi x10,x10,0
    addi a0, a0, CONST          # expect: addi x10,x10,42
    .set CONST, 42
    addi a0, a0, CONST + 1      # expect: addi x10,x10,43
    CONST2 = CONST * 2
    addi a0, a0, CONST2         # expect: addi x10,x10,84
    .equ CONST3, CONST2 - 80
    addi a0, a0, CONST3         # expect: addi x10,x10,4
# マクロ
    .macro twice op, r, imm=1
    \op \r, \r, \imm
    \op \r, \r, \imm
    .endm
    twice addi, a0              # expect: addi x10,x10,1 | addi x10,x10,1
    twice addi a1 3             # expect: addi x11,x11,3 | addi x11,x11,3
    .macro paste r
    add\()i \r, \r, 2
    .endm
    paste a2                    # expect: addi x12,x12,2
    .altmacro
    .macro loop r
    LOCAL here
here:
    bnez \r, here
    .endm
    loop a0                     # expect: bne x10,x0,.+0
    loop a1                     # expect: bne x11,x0,.+0
    .noaltmacro
# 条件
    .if 1
    addi a0, a0, 1              # expect: addi x10,x10,1
    .else
    addi a0, a0, 2
    .endif
    .if CONST - 42
    addi a0, a0, 3
    .else
    addi a0, a0, 4              # expect: addi x10,x10,4
    .endif
    .ifdef CONST
    addi a0, a0, 5              # expect: addi x10,x10,5
    .endif
    .ifndef NOT_DEFINED
    addi a0, a0, 6              # expect: addi x10,x10,6
    .endif
# 1 行に複数の文
    addi a0, a0, 7 ; addi a1, a1, 8     # expect: addi x10,x10,7 | addi x11,x11,8
    addi a0, a0, 9              # expect: addi x10,x10,9
end:
    .set LATER, 3
