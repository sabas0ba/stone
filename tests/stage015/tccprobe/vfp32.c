/* 可変部に「計算した」浮動小数点を渡す (第 6 部の実測で見つかった穴)。
 *
 * RV32 の ABI では可変部の double を整数レジスタ 2 本で渡す。左辺値
 * (記憶域にある変数) なら 2 語を読めばよいが，計算した値はレジスタに
 * しか無く，RV32 には浮動小数点レジスタから整数レジスタ 2 本への直接の
 * 移動が無い。いったんフレームへ落とさなければならない。
 * 直す前は「lvalue expected」で翻訳が止まっていた
 * (docs/stage015-riscv32.md 12 章)。
 *
 * libc は無い。UART (0x1000_0000) へ直に書き，test-finisher
 * (0x0010_0000) への書込みで止まる。入口は head.S。stdarg.h だけは
 * tcc 自身のものを使う (可変部の読み方は処理系の持ち物である)。
 */
#include <stdarg.h>

typedef unsigned long long u64;

static void putc_(int c) { *(volatile char *)0x10000000 = c; }

static void puthex(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i = i - 4) {
        int d = (v >> i) & 15;
        putc_(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

static void putd(double d)
{
    union { double d; u64 u; } w;
    w.d = d;
    puthex((unsigned)(w.u >> 32));
    puthex((unsigned)w.u);
}

/* 可変部を読む */
static double take1(int n, ...)
{
    va_list ap;
    double d;
    va_start(ap, n);
    d = va_arg(ap, double);
    va_end(ap);
    return d;
}

static double take2(int n, ...)
{
    va_list ap;
    double a;
    double b;
    va_start(ap, n);
    a = va_arg(ap, double);
    b = va_arg(ap, double);
    va_end(ap);
    return a + b * 100.0;
}

static unsigned tb = 3000;
static unsigned tt = 3;

void cmain(void)
{
    double v;

    /* 計算した double を可変部へ (これが翻訳できなかった) */
    putd(take1(1, (double)tb / 1000));          putc_(':');   /* 3.0 */

    /* 2 段の除算 (libtcc.c の統計出力と同じ形) */
    putd(take1(1, (double)tb / 1000 / tt));     putc_(':');   /* 1.0 */

    /* 左辺値の double も従来どおり通る */
    v = 2.5;
    putd(take2(2, v, (double)tt));              putc_(':');   /* 302.5 */

    /* float は既定の実引数拡張で double になる */
    putd(take1(1, (float)1.5f + 0.25f));        putc_('\n');  /* 1.75 */

    *(volatile int *)0x100000 = 0x5555;
    for (;;) ;
}
