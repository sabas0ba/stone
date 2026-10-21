/* psABI の照合 (abi.s と組にする)。期待は psABI の文面から手で書いた
 * (abi.expect)。ホストの gcc では組めないので ncc18.sh expect の対象外 */
int printf(const char *fmt, ...);
typedef __builtin_va_list va_list;

struct pt { int x, y; };
struct tri { int a, b, c; };

long long asm_add64(long long x, int y, long long z);
long long asm_split(int a, int b, int c, int d, int e, int f, int g, long long h);
int asm_small(struct pt p);
int asm_big(struct tri t);
struct tri asm_retbig(int k);
struct pt asm_retpt(void);
int asm_vcall1(void);
int asm_vcall2(void);
long long asm_ncall(void);
int asm_narrow(void);

int vsum(int n, ...)
{
    va_list ap;
    long long v;
    __builtin_va_start(ap, n);
    v = __builtin_va_arg(ap, long long);
    __builtin_va_end(ap);
    return n == 1 && v == 0x1122334455667788LL;
}

int vsum7(int a, int b, int c, int d, int e, int f, int g, ...)
{
    va_list ap;
    long long v;
    int k;
    __builtin_va_start(ap, g);
    v = __builtin_va_arg(ap, long long);
    k = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return a + b + c + d + e + f + g == 28 && v == 0x100000002LL && k == 9;
}

long long nsplit(int a, int b, int c, int d, int e, int f, int g, long long h)
{
    return a + b + c + d + e + f + g == 28 ? h : -1;
}

int narrow(signed char a, unsigned char b, short c, unsigned short d)
{
    return a == -2 && b == 254 && c == -3 && d == 65533;
}

int main(void)
{
    struct pt p = { 3, 4 };
    struct tri t = { 5, 6, 7 }, r;
    struct pt q;
    printf("add64 %llx\n", asm_add64(0x100000005LL, -1, 0x200000000LL));
    printf("split %llx\n", asm_split(1, 2, 3, 4, 5, 6, 7, 0x1234567890abcdefLL));
    printf("small %d big %d\n", asm_small(p), asm_big(t));
    r = asm_retbig(20);
    q = asm_retpt();
    printf("retbig %d %d %d retpt %d %d\n", r.a, r.b, r.c, q.x, q.y);
    printf("vcall %d %d\n", asm_vcall1(), asm_vcall2());
    printf("ncall %llx narrow %d\n", asm_ncall(), asm_narrow());
    return 0;
}
