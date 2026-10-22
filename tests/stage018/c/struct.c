/* 構造体・共用体・ビットフィールド・値渡しと値返し */
int printf(const char *fmt, ...);

struct pt { int x, y; };
struct s3 { char a, b, c; };
struct big { int v[5]; char tag; };
struct mix { char c; long long ll; short s; int i; };
struct bits {
    unsigned a : 3;
    signed b : 5;
    unsigned c : 12;
    int : 0;
    unsigned d : 7;
    signed char e : 4;
};
struct packed { char c; int i; short s; } __attribute__((packed));
union u { int i; unsigned char b[4]; short s; };
struct nest { struct pt p; struct s3 t; union u w; };
struct anon { int k; union { int ui; short us; }; struct { char p, q; }; };

static struct pt mkpt(int x, int y) { struct pt p; p.x = x; p.y = y; return p; }
static struct s3 mk3(char a) { struct s3 s; s.a = a; s.b = a + 1; s.c = a + 2; return s; }
static struct big mkbig(int k)
{
    struct big b;
    int i;
    for (i = 0; i < 5; i++)
        b.v[i] = k * i;
    b.tag = 'z';
    return b;
}
static int sumpt(struct pt p) { return p.x * 100 + p.y; }
static int sum3(struct s3 s) { return s.a + s.b + s.c; }
static int sumbig(struct big b) { return b.v[0] + b.v[1] + b.v[2] + b.v[3] + b.v[4] + b.tag; }
static int many(struct pt a, struct big b, struct s3 c, long long d, struct pt e)
{
    return a.x + b.v[4] + c.c + (int)d + e.y;
}

struct pt gp = { 3, 4 };

int main(void)
{
    struct pt p = { 1, 2 }, q;
    struct bits bf;
    struct packed pk;
    union u w;
    struct nest n;
    struct anon an;
    struct big b;
    struct pt arr[3];
    int i;

    printf("size %d %d %d %d %d %d %d %d\n", (int)sizeof(struct pt), (int)sizeof(struct s3),
           (int)sizeof(struct big), (int)sizeof(struct mix), (int)sizeof(struct bits),
           (int)sizeof(struct packed), (int)sizeof(union u), (int)sizeof(struct anon));
    printf("off %d %d %d %d\n", (int)__builtin_offsetof(struct mix, ll),
           (int)__builtin_offsetof(struct mix, s), (int)__builtin_offsetof(struct mix, i),
           (int)__builtin_offsetof(struct packed, s));
    q = p;
    q.x += 10;
    printf("assign %d %d %d %d\n", p.x, p.y, q.x, q.y);
    p = mkpt(5, 6);
    printf("ret %d %d %d\n", p.x, p.y, sumpt(mkpt(7, 8)));
    printf("ret3 %d %d\n", mk3('a').c, sum3(mk3(10)));
    b = mkbig(3);
    printf("big %d %d %c\n", b.v[4], sumbig(b), mkbig(2).tag);
    printf("many %d\n", many(p, b, mk3(1), 1000000LL, gp));
    bf.a = 5;
    bf.b = -7;
    bf.c = 4000;
    bf.d = 100;
    bf.e = -3;
    printf("bits %u %d %u %u %d\n", bf.a, bf.b, bf.c, bf.d, bf.e);
    bf.a += 4;
    bf.b -= 20;
    bf.c++;
    printf("bits2 %u %d %u\n", bf.a, bf.b, bf.c);
    pk.c = 1;
    pk.i = 0x12345678;
    pk.s = -2;
    printf("packed %d %x %d\n", pk.c, pk.i, pk.s);
    w.i = 0x04030201;
    printf("union %d %d %d\n", w.b[0], w.b[3], w.s);
    n.p = gp;
    n.t = mk3('x');
    n.w.i = 7;
    printf("nest %d %c %d\n", n.p.y, n.t.b, n.w.s);
    an.k = 1;
    an.ui = 0x10001;
    an.p = 'p';
    an.q = 'q';
    printf("anon %d %d %d %c%c\n", an.k, an.ui, an.us, an.p, an.q);
    for (i = 0; i < 3; i++)
        arr[i] = mkpt(i, i * i);
    printf("arr %d %d\n", arr[2].y, (&arr[1])->x);
    {
        struct pt *pp = &arr[2];
        pp->x = 42;
        printf("ptr %d %d\n", arr[2].x, (*pp).y);
    }
    return 0;
}
