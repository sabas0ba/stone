/* その他: 大きなフレーム，列挙，typedef，文式，組込み関数，文字定数，64 bit の switch，深い式 */
int printf(const char *fmt, ...);

typedef int (*binop_t)(int, int);
typedef struct { int k; const char *v; } entry_t;
enum color { RED, GREEN = 5, BLUE, BIG = 0x7fffffff };
enum neg { MINUS = -2, ZERO };

static int addf(int a, int b) { return a + b; }
static int mulf(int a, int b) { return a * b; }
static binop_t table[] = { addf, mulf };
static entry_t entries[] = { { 1, "one" }, { 2, "two" }, { 3, "three" } };

static int bigframe(int n)
{
    int big[1500];
    char huge[5000];
    int i, s = 0;
    for (i = 0; i < 1500; i++)
        big[i] = i;
    for (i = 0; i < 5000; i++)
        huge[i] = (char)i;
    for (i = 0; i < 1500; i += n)
        s += big[i];
    return s + huge[4999] + huge[2048];
}

static const char *lookup(int k)
{
    unsigned i;
    for (i = 0; i < sizeof entries / sizeof entries[0]; i++)
        if (entries[i].k == k)
            return entries[i].v;
    return "none";
}

static int sw64(long long v)
{
    switch (v) {
    case 0x100000000LL:
        return 1;
    case -1:
        return 2;
    case 7:
        return 3;
    }
    return 0;
}

static int deep(int a, int b, int c, int d)
{
    return ((a + b) * (c - d) + (a * b - c * d) * ((a ^ b) + (c | d)))
        - ((a << 2) + (b >> 1)) * ((c & 7) + (d % 5) + (a - (b - (c - (d - (a + 1))))));
}

int main(void)
{
    int x = 10, y;
    volatile int vx = 10;   /* 最適化しても定数と判定されない値 (constant_p の比較用) */
    unsigned char uc = '\xff';
    const char *esc = "tab\there\\ \"q\" \101\x42";
    enum color c = BLUE;
    printf("bigframe %d %d\n", bigframe(1), bigframe(7));
    printf("enum %d %d %d %d %d\n", RED, GREEN, c, BIG, MINUS + ZERO);
    printf("sizeof enum %d %d\n", (int)sizeof(enum color), (int)sizeof(enum neg));
    printf("table %d %d\n", table[0](3, 4), table[1](3, 4));
    printf("lookup %s %s\n", lookup(2), lookup(9));
    y = ({ int t = x * 2; t + 1; });
    printf("stmtexpr %d\n", y);
    printf("builtin %d %d %d %d\n", __builtin_constant_p(3 * 4), __builtin_constant_p(vx),
           __builtin_types_compatible_p(int, const int), __builtin_choose_expr(1, 10, 20));
    printf("char %d %d %d\n", uc, '\xff', 'A' + 1);
    printf("esc %s\n", esc);
    printf("comma %d\n", (x++, x++, x));
    printf("sw64 %d %d %d %d\n", sw64(0x100000000LL), sw64(-1), sw64(7), sw64(8));
    printf("deep %d %d\n", deep(1, 2, 3, 4), deep(-7, 13, 100, -3));
    {
        int arr[3] = { 1, 2, 3 }, *pa = arr, **ppa = &pa;
        **ppa += 10;
        (*ppa)[2] *= 5;
        printf("pp %d %d %d\n", arr[0], arr[1], arr[2]);
    }
    {
        struct { int a; int b; } s1 = { 1, 2 }, s2 = { 3, 4 }, s3;
        s3 = x > 5 ? s1 : s2;
        printf("condstruct %d %d\n", s3.a, s3.b);
    }
    {
        int sh = 5;
        unsigned u = 0x80000001u;
        printf("varshift %u %d %u\n", u >> sh, -64 >> sh, u << (sh + 20));
    }
    return 0;
}
