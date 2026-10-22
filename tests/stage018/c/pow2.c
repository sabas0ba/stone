/* 64 bit の 2 の冪での除算と剰余 (docs/stage018-ncc.md 15 章)。実行時支援を
 * 呼ばずにシフトとマスクで作る。符号付きは 0 の方へ丸める */
int printf(const char *fmt, ...);

typedef long long s64;
typedef unsigned long long u64;

static void show(const char *tag, u64 v)
{
    printf("%s %08x%08x\n", tag, (unsigned)(v >> 32), (unsigned)v);
}

int main(void)
{
    static const s64 sv[] = { 0, 1, -1, 7, -7, 4095, -4095, 4096, -4097,
                              0x123456789abcdefLL, -0x123456789abcdefLL,
                              0x7fffffffffffffffLL, -0x7fffffffffffffffLL - 1 };
    unsigned i;
    for (i = 0; i < sizeof(sv) / sizeof(sv[0]); i++) {
        s64 x = sv[i];
        u64 u = (u64)x;
        printf("[%u]\n", i);
        show("s/1", (u64)(x / 1));
        show("s%1", (u64)(x % 1));
        show("s/2", (u64)(x / 2));
        show("s%2", (u64)(x % 2));
        show("s/4096", (u64)(x / 4096));
        show("s%4096", (u64)(x % 4096));
        show("s/2^31", (u64)(x / 0x80000000LL));
        show("s%2^31", (u64)(x % 0x80000000LL));
        show("s/2^32", (u64)(x / 0x100000000LL));
        show("s%2^32", (u64)(x % 0x100000000LL));
        show("s/2^33", (u64)(x / 0x200000000LL));
        show("s%2^33", (u64)(x % 0x200000000LL));
        show("s/2^40", (u64)(x / 0x10000000000LL));
        show("s%2^40", (u64)(x % 0x10000000000LL));
        show("u/2", u / 2);
        show("u%2", u % 2);
        show("u/4096", u / 4096);
        show("u%4096", u % 4096);
        show("u/2^32", u / 0x100000000ULL);
        show("u%2^32", u % 0x100000000ULL);
        show("u/2^40", u / 0x10000000000ULL);
        show("u%2^40", u % 0x10000000000ULL);
    }
    return 0;
}
