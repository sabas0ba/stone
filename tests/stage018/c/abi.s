# psABI の照合 (docs/stage018-ncc.md 7 章の 3)。abi.c と組にする。
#
# ここの関数は psABI 1.0 (riscv-cc.adoc の Integer Calling Convention) の
# 文面どおりに引数を読み書きするように手で書いた。ncc1 の訳した C と
# 呼び合い，両者の解釈が一致することを見る。各関数の注釈がその規則である。

    .text

# long long asm_add64(long long x, int y, long long z)
# 2xXLEN の scalar は対で渡す (下位が若い番号): x = a0/a1，y = a2，z = a3/a4
# 戻り値も対: a0/a1
    .globl asm_add64
asm_add64:
    add t0, a0, a3          # 下位
    sltu t1, t0, a0
    add t2, a1, a4          # 上位
    add t2, t2, t1
    add a0, t0, a2          # + y (符号拡張して足す)
    sltu t1, a0, t0
    add t2, t2, t1
    srai t3, a2, 31
    add a1, t2, t3
    ret

# long long asm_split(int a, int b, int c, int d, int e, int f, int g, long long h)
# 1 本しか残らなければ下位をレジスタ (a7)，上位をスタック (入口の sp + 0)
    .globl asm_split
asm_split:
    lw a1, 0(sp)
    mv a0, a7
    ret

# int asm_small(struct { int x, y; } p)   2xXLEN 以下の集成体はレジスタ: x = a0，y = a1
    .globl asm_small
asm_small:
    li t0, 10
    mul a0, a0, t0
    add a0, a0, a1
    ret

# int asm_big(struct { int a, b, c; } p)  2xXLEN を超える集成体は番地で: a0 = 写しの番地
    .globl asm_big
asm_big:
    lw t0, 0(a0)
    lw t1, 4(a0)
    lw t2, 8(a0)
    add a0, t0, t1
    add a0, a0, t2
    ret

# struct { int a, b, c; } asm_retbig(int k)
# 番地で渡す大きさの戻り値は，呼ぶ側の領域の番地が隠れた第 1 引数 (a0)。k は a1
    .globl asm_retbig
asm_retbig:
    sw a1, 0(a0)
    addi t0, a1, 1
    sw t0, 4(a0)
    addi t0, a1, 2
    sw t0, 8(a0)
    ret

# struct { int x, y; } asm_retpt(void)   2xXLEN 以下の戻り値は a0/a1
    .globl asm_retpt
asm_retpt:
    li a0, 31
    li a1, 41
    ret

# int asm_vcall1(void): vsum(1, (long long)0x1122334455667788) を呼ぶ
# 可変長の 2xXLEN の整列を持つ引数は偶数番からの対: a1 を飛ばして a2/a3
    .globl asm_vcall1
asm_vcall1:
    addi sp, sp, -16
    sw ra, 12(sp)
    li a0, 1
    li a1, 0x7777           # 使われない (飛ばされたレジスタ)
    li a2, 0x55667788
    li a3, 0x11223344
    call vsum
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

# int asm_vcall2(void): vsum7(1, 2, 3, 4, 5, 6, 7, (long long)0x100000002, 9)
# 名前付き 7 個で a0〜a6。可変長の 64 bit は a7 が奇数番なので対にならず
# スタック (sp + 0，8 バイト境界) へ。以降の引数もすべてスタック (sp + 8)
    .globl asm_vcall2
asm_vcall2:
    addi sp, sp, -32
    sw ra, 28(sp)
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 0x7777           # 使われない
    li t0, 2
    sw t0, 0(sp)
    li t0, 1
    sw t0, 4(sp)
    li t0, 9
    sw t0, 8(sp)
    call vsum7
    lw ra, 28(sp)
    addi sp, sp, 32
    ret

# long long asm_ncall(void): nsplit(1, 2, 3, 4, 5, 6, 7, (long long)0x500000006) を呼ぶ
# 名前付きの 64 bit が a7 とスタックに分かれる
    .globl asm_ncall
asm_ncall:
    addi sp, sp, -16
    sw ra, 12(sp)
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 6
    li t0, 5
    sw t0, 0(sp)
    call nsplit
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

# int asm_narrow(void): 狭い整数の引数は型の符号で 32 bit へ広げて渡す
# narrow(signed char -2, unsigned char 254, short -3, unsigned short 65533)
    .globl asm_narrow
asm_narrow:
    addi sp, sp, -16
    sw ra, 12(sp)
    li a0, -2
    li a1, 254
    li a2, -3
    li a3, 65533
    call narrow
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
