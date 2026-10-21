/* GNU C の拡張 (docs/stage018-ncc.md 10 章の M4): __label__ の局所のラベル，
 * ラベルの付いた文式の値，return を含む文式，長い関数の条件分岐，余る
 * 初期化子，番地の比較を畳む静的な初期化子，指示子の上書き，複合リテラル
 * の静的な初期化子，同じ型への構造体のキャスト，到達の判定 */
int printf(const char *fmt, ...);

struct lk { int a; };
struct mx { int o; struct lk w; int l; };

static int twice(int v) { return v * 2; }
static int (*const pick)(int) = (twice == (int (*)(int))0) ? 0 : twice;
static int (*const pick2)(int) = (twice != (int (*)(int))0) ? twice : 0;
static int gvar = 7;
static const char *names[2] = { [0] = "a", [1] = "b", [0] = "c" };
static const char *ord[3] = { [2] = "two", [0] = "zero", [1] = "one" };
static int *ptrs[2] = { [0] = &gvar, [1] = &gvar, [0] = 0 };
static int z0[0] = { 0 };
static int a2[2] = { 1, 2, 3 };
static struct mx m1 = { .o = 1, .w = (struct lk) { }, .l = 3 };
static struct mx m2 = { .o = 4, .w = (struct lk) { 5 }, .l = 6 };
static struct lk lk2 = (struct lk) { 8 };

static int locallabels(int n)
{
    int s = 0;
    s += ({ __label__ out; int r = n; if (r > 3) goto out; r += 100; out: r; });
    s += ({ __label__ out; int r = n * 2; if (r > 3) goto out; r += 1000; out: r; });
    return s;
}

static int labelvalue(void)
{
    unsigned long p = ({ __label__ here; here: (unsigned long)&&here; });
    return p != 0;
}

static int early(int n)
{
    int v = ({ if (n < 0) return -1; n * 3; });
    int w = ({ int t = n; if (t > 100) { return 100; } t + 1; });
    int q = n > 50 ? ({ return 50; 0; }) : n;
    return v + w + q;
}

static int reach(int k)
{
    int v = 0;
    v += ({ int r = 0; do { r = 1; } while (0); r; });
    v += ({ int u; if (1) u = 4; else u = 5; u; });
    v += ({ int u; if (0) u = 40; else u = 50; u; });
    switch (k) {
    case 1:
        do {
            if (k)
                break;
            v = 900;
        } while (0);
        v += 1;
        break;
    default:
        v += 2;
    }
    {
        int i;
        for (i = 0; i < 4; i++)
            do {
                if (i & 1)
                    continue;
                v += 10;
            } while (0);
    }
    while (1) {
        v += 1000;
        break;
    }
    return v;
}

static unsigned longloop(unsigned n)
{
    unsigned x = 1, i;
    for (i = 0; i < n; i++) {
        if (x & 1) {
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
            x = x * 4 + i;
            x = x * 5 + i;
            x = x * 6 + i;
            x = x * 7 + i;
            x = x * 8 + i;
            x = x * 9 + i;
            x = x * 10 + i;
            x = x * 11 + i;
            x = x * 12 + i;
            x = x * 13 + i;
            x = x * 14 + i;
            x = x * 15 + i;
            x = x * 3 + i;
        } else {
            x ^= x >> 3;
        }
        switch (x & 3) {
        case 0:
            x += 7;
            break;
        case 2:
            x += 11;
            break;
        }
    }
    return x;
}

int main(void)
{
    char sized[(sizeof(int) == sizeof(*(8 ? ((void *)((long)(2 * sizeof(struct mx) + sizeof("abc")) * 0l)) : (int *)8))) ? 3 : 9];
    struct lk c = (struct lk)(lk2);
    unsigned long zl[0] = { 0 };
    printf("labels %d %d %d\n", locallabels(1), locallabels(5), labelvalue());
    printf("early %d %d %d\n", early(-4), early(4), early(500));
    printf("reach %d %d\n", reach(1), reach(2));
    printf("long %u %u\n", longloop(10), longloop(1000));
    printf("pick %d %d\n", pick(4), pick2(5));
    printf("over %s %s %d\n", names[0], names[1], ptrs[0] == 0 && *ptrs[1] == 7);
    printf("ord %s %s %s\n", ord[0], ord[1], ord[2]);
    printf("excess %d %d %d %d %d\n", (int)sizeof(z0), a2[0], a2[1], (int)sizeof(a2), (int)sizeof(zl));
    printf("complit %d %d %d %d %d %d %d\n", m1.o, m1.w.a, m1.l, m2.o, m2.w.a, m2.l, lk2.a);
    printf("cast %d constexpr %d\n", c.a, (int)sizeof(sized));
    return 0;
}
