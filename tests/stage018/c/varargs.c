/* 可変長引数と多数の引数 (psABI の割付け: a7 とスタックの分割・偶数番の対) */
int printf(const char *fmt, ...);
typedef __builtin_va_list va_list;

static long long sumv(int n, ...)
{
    va_list ap;
    long long s = 0;
    int i;
    __builtin_va_start(ap, n);
    for (i = 0; i < n; i++) {
        int kind = __builtin_va_arg(ap, int);
        if (kind == 1)
            s += __builtin_va_arg(ap, long long);
        else
            s += __builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
    return s;
}

static int ten(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}

/* 7 番目の 64 bit の引数は a7 とスタックに分かれる */
static long long split(int a, int b, int c, int d, int e, int f, int g, long long h, int i)
{
    return a + b + c + d + e + f + g + h + i;
}

static long long tail64(int a, long long b, long long c, long long d, long long e)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5;
}

struct pt { int x, y; };
static int vstruct(int n, ...)
{
    va_list ap;
    int s = 0;
    __builtin_va_start(ap, n);
    while (n--) {
        struct pt p = __builtin_va_arg(ap, struct pt);
        s += p.x * 10 + p.y;
    }
    __builtin_va_end(ap);
    return s;
}

int main(void)
{
    struct pt a = { 1, 2 }, b = { 3, 4 };
    printf("sumv %lld\n", sumv(4, 0, 5, 1, 10000000000LL, 0, 7, 1, -3LL));
    /* 名前付きの引数のあとに 7 個の int: 64 bit の値は a7 を飛ばしてスタックへ */
    printf("sumv2 %lld\n", sumv(3, 0, 1, 0, 2, 1, 123456789012LL));
    printf("ten %d\n", ten(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    printf("split %lld\n", split(1, 2, 3, 4, 5, 6, 7, 0x100000000LL, 9));
    printf("tail64 %lld\n", tail64(1, 2, 3, 4, 5));
    printf("vstruct %d\n", vstruct(2, a, b));
    printf("mix %d %lld %d %lld %s %c\n", 1, 2LL, 3, -4LL, "five", '6');
    printf("pad [%5d] [%-5d] [%05d] [%x] [%08X] [%3s] [%-3s]\n", 42, 42, 42, 0xbeef, 0xbeef, "a", "b");
    return 0;
}
