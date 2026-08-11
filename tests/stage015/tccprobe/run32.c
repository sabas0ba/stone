/* riscv32-tcc の生成コードを**実際に走らせて**答を見る (その 2 の実走)。
 *
 * 逆アセンブルして不正命令が無いことを見るだけでは，命令が正しく
 * 選ばれていることまでは判らない。ここでは値を計算して 16 進で出す。
 * 期待値は tests/stage015/test.sh に置いてある。
 *
 * libc は無い。UART (0x1000_0000) へ直に書き，test-finisher
 * (0x0010_0000) への書込みで止まる。入口は head.S。
 *
 * 注意: RISC-V の ABI では素の char は**符号なし**である
 * (tcc の CHAR_IS_UNSIGNED)。cb の -5 は 251 として足される。 */

static void putc_(int c) { *(volatile char *)0x10000000 = c; }

static void puthex(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i = i - 4) {
        int d = (v >> i) & 15;
        putc_(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

int mul(int a, int b) { return a * b; }
int dv(int a, int b) { return a / b; }
int md(int a, int b) { return a % b; }
int shl(int a, int n) { return a << n; }
int sar(int a, int n) { return a >> n; }
int shr(unsigned a, int n) { return a >> n; }

char cb = -5;
short sh = -300;
unsigned char ub = 200;
unsigned short us = 60000;

int sum(int n)
{
    int i, s = 0;
    for (i = 1; i <= n; i++)
        s += i;
    return s;
}

int tab[8];

int arr(int n)
{
    int i, s = 0;
    for (i = 0; i < 8; i++)
        tab[i] = i * n;
    for (i = 0; i < 8; i++)
        s += tab[i];
    return s;
}

void cmain(void)
{
    puthex(mul(1234, 5678));      putc_(':');
    puthex(dv(-1000, 7));         putc_(':');
    puthex(md(1000, 7));          putc_(':');
    puthex(shl(3, 20));           putc_(':');
    puthex(sar(-4096, 5));        putc_(':');
    puthex(shr(0xf0000000u, 8));  putc_(':');
    puthex(cb + sh + ub + us);    putc_(':');
    puthex(sum(100));             putc_(':');
    puthex(arr(3));               putc_('\n');
    *(volatile int *)0x100000 = 0x5555;   /* test-finisher: 終了コード 0 */
    for (;;)
        ;
}
