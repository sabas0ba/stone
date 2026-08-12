/* ilp32 の呼出し規約: long long (その 3)。
 * レジスタ 2 本にまたがる値の受け渡し・返却，桁上げ付きの加減算，
 * 倍幅乗算，可変桁のシフト (実行時支援の呼出し)，スタックへ溢れる
 * 引数を通す。期待値は tests/stage015/test.sh にある。 */

static void putc_(int c) { *(volatile char *)0x10000000 = c; }
static void puthex(unsigned v) { int i; for (i=28;i>=0;i=i-4){int d=(v>>i)&15;putc_(d<10?'0'+d:'a'+d-10);} }
static void puthex64(unsigned long long v) { puthex((unsigned)(v >> 32)); puthex((unsigned)v); }
long long llmul(long long a, long long b) { return a * b; }
long long lladd(long long a, int b, long long c) { return a + b + c; }
long long llsub(long long a, long long b) { return a - b; }
long long llshift(long long a, int n) { return (a << n) + (a >> n); }
int llcmp(long long a, long long b) { return (a < b) + 2 * (a == b) + 4 * (a > b); }
int many(int a,int b,int c,int d,int e,int f,int g,int h,int i,int j) { return a+b*2+c*3+d*4+e*5+f*6+g*7+h*8+i*9+j*10; }
long long mixed(int a, long long b, int c, long long d) { return a + b + c + d; }
void cmain(void) {
    puthex64(llmul(1000000007LL, 1234567LL)); putc_(':');
    puthex64(lladd(0x100000000LL, -3, 5LL));  putc_(':');
    puthex64(llsub(0x100000000LL, 1LL));      putc_(':');
    puthex64(llshift(0x123456789LL, 5));      putc_(':');
    puthex(llcmp(1LL, 0x100000000LL));        putc_(':');
    puthex(many(1,2,3,4,5,6,7,8,9,10));       putc_(':');
    puthex64(mixed(1, 0x200000000LL, 2, 3LL));putc_('\n');
    *(volatile int *)0x100000 = 0x5555;
    for (;;) ;
}
