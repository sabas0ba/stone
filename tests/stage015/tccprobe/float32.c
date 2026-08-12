/* 浮動小数点 (その 4)。
 * float / double の四則と比較，整数との行き来 (32 bit と 64 bit の
 * 両方向)，単精度と倍精度の行き来を通す。値は bit の並びで出すので
 * 丸めの違いもそのまま見える。
 *
 * 64 bit 整数との変換は RV32 に命令が無いので実行時支援を呼ぶ
 * (docs/stage015-riscv32.md 13 章)。 */

static void putc_(int c) { *(volatile char *)0x10000000 = c; }
static void puthex(unsigned v) { int i; for (i=28;i>=0;i=i-4){int d=(v>>i)&15;putc_(d<10?'0'+d:'a'+d-10);} }

union fu { float f; unsigned u; };
union du { double d; unsigned long long ull; };

static void putf(float f) { union fu x; x.f = f; puthex(x.u); }
static void putd(double d) { union du x; x.d = d; puthex((unsigned)(x.ull >> 32)); puthex((unsigned)x.ull); }

float fadd(float a, float b) { return a + b; }
float fmul(float a, float b) { return a * b; }
double dadd(double a, double b) { return a + b; }
double dmul(double a, double b) { return a * b; }
double ddiv(double a, double b) { return a / b; }
int dcmp(double a, double b) { return (a < b) + 2 * (a == b) + 4 * (a > b); }
double i2d(int i) { return (double)i; }
int d2i(double d) { return (int)d; }
double ll2d(long long v) { return (double)v; }
long long d2ll(double d) { return (long long)d; }
float d2f(double d) { return (float)d; }
double f2d(float f) { return (double)f; }

void cmain(void) {
    putf(fadd(1.5f, 2.25f));    putc_(':');   /* 3.75 */
    putf(fmul(3.0f, 0.5f));     putc_(':');   /* 1.5  */
    putd(dadd(1.5, 2.25));      putc_(':');   /* 3.75 */
    putd(dmul(3.0, 0.5));       putc_(':');   /* 1.5  */
    putd(ddiv(1.0, 4.0));       putc_(':');   /* 0.25 */
    puthex(dcmp(1.0, 2.0));     putc_(':');   /* 1 */
    putd(i2d(-7));              putc_(':');   /* -7.0 */
    puthex((unsigned)d2i(-7.9));putc_(':');   /* -7 */
    putd(ll2d(1234567890123LL));putc_(':');
    puthex((unsigned)d2ll(1e12)); putc_(':');
    putf(d2f(0.5));             putc_(':');   /* 0.5f */
    putd(f2d(0.5f));            putc_('\n');  /* 0.5  */
    *(volatile int *)0x100000 = 0x5555;
    for (;;) ;
}
