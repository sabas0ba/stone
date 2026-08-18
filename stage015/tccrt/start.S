/* start.S --- tcc の ABI で走る実行環境の土台 (Stage 15 第 6 部)
 *
 * 我々の cc とは呼出し規約が違う。cc の生成コードは実引数をデータ
 * スタック (x9) に積むが，tcc は標準の RV32 ilp32 ABI (a0..a7) を使う。
 * したがって **tcc が作る実行形式に cc が作った .o は混ぜられない**。
 * 起動部・syscall スタブ・libc をすべて tcc の側で用意する
 * (docs/stage015-tcc.md 12.11)。
 *
 * これは ld15 の 'E' 前置部が機械語で持っていたものと同じ働きを，
 * tcc のアセンブラで書き直したものである。tcc は自前の RISC-V
 * アセンブラを持つので，これも T1 が読める。
 *
 * syscall の規約はカーネルと同じ RV32 Linux 互換 —— a7 = 番号，
 * a0..a2 = 引数，a0 = 結果 (失敗は -errno)。C 側の包み (libc15 の
 * posix/sys.c) が errno へ写す。
 */

    .text
    .globl _start
_start:
    /* カーネル (kernel16 の setupstack) は sp に [argc][argv...][0][0]
     * を積んで U モードへ渡す。ABI どおり a0 = argc, a1 = argv で main
     * を呼ぶ */
    lw   a0, 0(sp)
    addi a1, sp, 4
    jal  main
    /* main の返り値でそのまま exit へ落ちる */

/* exit は libc15 に実体が無い ('E' 前置部が機械語で持っていた)。
 * tcc の ABI で組むときはここが実体である */
    .globl exit
exit:
    li   a7, 93
    ecall
1:  j    1b

/* ---- 生の syscall スタブ ----
 *
 * どれも「a7 に番号を置いて ecall」だけである。引数は呼び手が既に
 * a0..a2 へ置いている (ABI が同じなので詰め替えが要らない)。
 */

    .globl sys_read
sys_read:                       /* (fd, buf, n) */
    li   a7, 63
    ecall
    ret

    .globl sys_write
sys_write:                      /* (fd, buf, n) */
    li   a7, 64
    ecall
    ret

    .globl sys_close
sys_close:                      /* (fd) */
    li   a7, 57
    ecall
    ret

    .globl sys_brk
sys_brk:                        /* (addr) */
    li   a7, 214
    ecall
    ret

    .globl sys_openat
sys_openat:                     /* (dirfd, path, flags, mode) */
    li   a7, 56
    ecall
    ret

/* 汎用スタブ。番号を第 1 引数で受けるので 1 つずつ詰め替える
 * (libc15 が lseek / spawn をこれで包む) */
    .globl sys_ecall
sys_ecall:                      /* (n, a, b, c) */
    mv   a7, a0
    mv   a0, a1
    mv   a1, a2
    mv   a2, a3
    ecall
    ret
