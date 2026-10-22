/* 64 bit の整数 (long long) */
int printf(const char *fmt, ...);

long long g_ll = -1234567890123LL;
unsigned long long g_ull = 0xfedcba9876543210ULL;

static long long twice(long long x) { return x * 2; }
static unsigned long long mix(int a, long long b, int c, unsigned long long d)
{
    return (unsigned long long)a + b + c + d;
}

int main(void)
{
    long long a = 123456789012LL, b = -98765LL, c;
    unsigned long long u = 0xffffffffffULL, v = 3;
    int i;
    printf("add %lld sub %lld mul %lld\n", a + b, a - b, a * b);
    printf("div %lld mod %lld\n", a / b, a % b);
    printf("udiv %llu umod %llu\n", u / v, u % v);
    printf("hex %llx %llX\n", g_ull, u);
    for (i = 0; i < 70; i += 13)
        printf("shift %d %lld %lld %llu\n", i, a << i, b >> i, g_ull >> i);
    printf("cmp %d %d %d %d\n", a < b, a > b, b < 0, u > 0xfffffffeULL);
    printf("cmp2 %d %d\n", (long long)-1 < 1LL, (unsigned long long)-1 > 1ULL);
    printf("and %llx or %llx xor %llx not %llx\n", g_ull & u, g_ull | 1, g_ull ^ u, ~g_ull);
    printf("neg %lld %lld\n", -a, -g_ll);
    c = a;
    c += b;
    c *= 3;
    c -= 1;
    c /= 7;
    printf("opassign %lld\n", c);
    printf("conv %lld %llu %d %u\n", (long long)-5, (unsigned long long)-5, (int)g_ll, (unsigned)g_ull);
    printf("ext %lld %lld\n", (long long)(signed char)-3, (long long)(unsigned char)250);
    printf("call %lld %llu\n", twice(a), mix(1, 2, 3, 4));
    c = g_ll++;
    printf("incdec %lld", c);
    printf(" %lld\n", ++g_ll);
    printf("bool %d %d\n", !g_ll, !(g_ll - g_ll));
    {
        unsigned long long f = 1;
        for (i = 1; i <= 20; i++)
            f *= i;
        printf("fact %llu\n", f);
    }
    return 0;
}
