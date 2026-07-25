# Stage 3 全命令エンコードテスト (docs/stage003-asm.md 5 章 2)。
# RV32IM 全 48 命令 + 全疑似命令 + ディレクティブを網羅する。
# 期待バイナリは rv32im-expected.hex (hex0 言語による手エンコード)。
top:
  add x1 x2 x3
  sub x31 x30 x29
  sll x4 x5 x6
  slt x7 x8 x9
  sltu x10 x11 x12
  xor x13 x14 x15
  srl x16 x17 x18
  sra x19 x20 x21
  or x22 x23 x24
  and x25 x26 x27
  mul x1 x2 x3
  mulh x4 x5 x6
  mulhsu x7 x8 x9
  mulhu x10 x11 x12
  div x13 x14 x15
  divu x16 x17 x18
  rem x19 x20 x21
  remu x22 x23 x24
  addi x1 x2 -2048
  slti x3 x4 2047
  sltiu x5 x6 0xff
  xori x7 x8 -1
  ori x9 x10 0x7f
  andi x11 x12 255
  slli x13 x14 0
  srli x15 x16 31
  srai x17 x18 1
  lb x1 -1(x2)
  lh x3 2(x4)
  lw x5 0x10(x6)
  lbu x7 100(x8)
  lhu x9 -2048(x10)
  jalr x11 2047(x12)
  sb x1 -1(x2)
  sh x3 30(x4)
  sw x5 -30(x6)
  beq x1 x2 top
  bne x3 x4 top
  blt x5 x6 bot
  bge x7 x8 bot
  bltu x9 x10 top
  bgeu x11 x12 bot
  lui x13 0xfffff
  auipc x14 0
  jal x15 top
  fence
  ecall
  ebreak
bot:
  nop
  mv x1 x31
  li x2 0
  li x3 -1
  li x4 0x12345678
  li x5 2047
  li x6 -2048
  la x7 top
  la x8 bot
  j top
  call bot
  jr x9
  ret
  beqz x10 bot
  bnez x11 top
  word 0xdeadbeef
  word -1
  word top
  word bot
  byte 0
  byte 255
  byte -128
  byte 0x7f
.
