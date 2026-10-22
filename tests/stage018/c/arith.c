/* 32 bit 以下の整数の演算・変換・昇格 */
int printf(const char *fmt, ...);

int g_i = -7;
unsigned g_u = 0xfffffff0u;
signed char g_sc = -100;
unsigned char g_uc = 200;
short g_s = -30000;
unsigned short g_us = 60000;

static int add(int a, int b) { return a + b; }

int main(void)
{
    int a = 17, b = -5, i, j, k, l;
    unsigned u = 3000000000u, v = 7;
    char c = 'A';
    signed char sc = g_sc;
    unsigned char uc = g_uc;
    short s = g_s;
    unsigned short us = g_us;

    printf("add %d sub %d mul %d\n", a + b, a - b, a * b);
    printf("div %d mod %d div2 %d mod2 %d\n", a / b, a % b, -a / 5, -a % 5);
    printf("udiv %u umod %u\n", u / v, u % v);
    printf("shl %d shr %d ushr %u sar %d\n", a << 3, a >> 2, u >> 7, b >> 1);
    printf("and %d or %d xor %d not %d\n", a & 12, a | 12, a ^ 12, ~a);
    printf("cmp %d %d %d %d %d %d\n", a < b, a > b, a <= 17, a >= 18, a == 17, a != 17);
    printf("ucmp %d %d\n", u > 5u, (unsigned)b > 5u);
    printf("mixed %d %u\n", b < (int)v, (unsigned)b + v);
    printf("char %d %d %d\n", c, sc, uc);
    printf("short %d %d\n", s, us);
    printf("trunc %d %d %d\n", (signed char)300, (unsigned char)300, (short)70000);
    printf("promote %d %d\n", uc + uc, sc * 2);
    printf("globals %d %u %d %d %d %d\n", g_i, g_u, g_sc, g_uc, g_s, g_us);
    printf("not %d %d\n", !a, !0);
    printf("neg %d %u\n", -a, -u);
    g_i += 10;
    g_u -= 1;
    g_sc *= 2;
    g_uc += 100;
    printf("opassign %d %u %d %d\n", g_i, g_u, g_sc, g_uc);
    i = 5;
    j = i++;
    k = ++i;
    l = i--;
    printf("incdec %d %d %d %d\n", j, k, l, i);
    printf("call %d\n", add(add(1, 2), add(3, 4)));
    {
        int x = 1000;
        x <<= 3;
        x >>= 1;
        x %= 7;
        x |= 0x40;
        x &= 0x7f;
        x ^= 3;
        printf("ops %d\n", x);
    }
    {
        unsigned int m = 0x80000000u;
        int n = (int)m;
        printf("conv %d %u %d\n", n, (unsigned)n, n >> 31);
    }
    printf("bool %d %d\n", (_Bool)5, (_Bool)0);
    return 0;
}
