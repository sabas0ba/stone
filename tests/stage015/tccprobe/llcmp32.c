/* ilp32 の呼出し規約: long long の比較 (その 3)。
 * 上位語で決まる場合と，上位語が等しく下位語で決まる場合の両方を通す。
 * tcc の gen_opl は「直前の比較の旗が残っている」前提で書かれており，
 * 旗の無い RISC-V ではそこを作り直した (docs/stage015-riscv32.md 12.3)。
 * ここはその作り直しが効いていることを見る。 */

static void putc_(int c) { *(volatile char *)0x10000000 = c; }
static void puthex(unsigned v) { int i; for (i=28;i>=0;i=i-4){int d=(v>>i)&15;putc_(d<10?'0'+d:'a'+d-10);} }
int lt(long long a, long long b) { return a < b; }
int gt(long long a, long long b) { return a > b; }
int eq(long long a, long long b) { return a == b; }
int ne(long long a, long long b) { return a != b; }
int ult(unsigned long long a, unsigned long long b) { return a < b; }
void cmain(void) {
    puthex(lt(1LL, 0x100000000LL));  putc_(':');   /* 1 */
    puthex(lt(0x100000000LL, 1LL));  putc_(':');   /* 0 */
    puthex(lt(1LL, 2LL));            putc_(':');   /* 1 */
    puthex(lt(-1LL, 1LL));           putc_(':');   /* 1 */
    puthex(gt(0x100000000LL, 1LL));  putc_(':');   /* 1 */
    puthex(eq(5LL, 5LL));            putc_(':');   /* 1 */
    puthex(ne(5LL, 6LL));            putc_(':');   /* 1 */
    puthex(ult(1LL, 0x100000000LL)); putc_('\n');  /* 1 */
    *(volatile int *)0x100000 = 0x5555;
    for (;;) ;
}
