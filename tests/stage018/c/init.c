/* 初期化子: 静的と局所，入れ子，中括弧の省略，指定付き，文字列，番地定数 */
int printf(const char *fmt, ...);

struct pt { int x, y; };
struct rec { const char *name; int vals[3]; struct pt at; };

int ga[5] = { 1, 2, 3 };
int gb[] = { 10, 20, 30, 40 };
char gs[8] = "abc";
char gt[] = "hello";
const char *gp = "ptr";
struct rec gr = { "rec", { 4, 5 }, { 6, 7 } };
struct rec grs[2] = { { "a", { 1 } }, { "b", { 2, 3, 4 }, { 8, 9 } } };
struct pt elided[2] = { 1, 2, 3, 4 };
int des[6] = { [2] = 22, [4] = 44, 55 };
struct rec gd = { .at = { .y = 77 }, .name = "des", .vals[1] = 11 };
int range[8] = { [1 ... 3] = 9, [6] = 6 };
int *gip = &gb[2];
struct pt *gpp = &elided[1];
int *gmem = &gr.at.y;
long long gll = 0x123456789abcdef0LL;
short gsh[3] = { -1, 2, -3 };
unsigned char guc[4] = { 255, 128, 1 };
struct { char c; int i; } gpad = { 'q', -9 };
int zeros[100];

static int counter(void)
{
    static int n = 100;
    return n++;
}

int main(void)
{
    int la[4] = { 7, 8 };
    char ls[] = "local";
    struct pt lp = { .y = 3 };
    struct rec lr = { "lr", { 1, 2, 3 }, { 4 } };
    int lm[2][3] = { { 1, 2 }, { 3 } };
    int i, s = 0;
    printf("ga %d %d %d %d %d\n", ga[0], ga[2], ga[3], ga[4], (int)sizeof ga);
    printf("gb %d %d\n", gb[3], (int)(sizeof gb / sizeof gb[0]));
    printf("gs %s %d %s %d\n", gs, (int)sizeof gs, gt, (int)sizeof gt);
    printf("gp %s\n", gp);
    printf("gr %s %d %d %d %d %d\n", gr.name, gr.vals[0], gr.vals[1], gr.vals[2], gr.at.x, gr.at.y);
    printf("grs %s %d %d %s %d %d\n", grs[0].name, grs[0].vals[0], grs[0].at.y, grs[1].name,
           grs[1].vals[2], grs[1].at.x);
    printf("elided %d %d %d %d\n", elided[0].x, elided[0].y, elided[1].x, elided[1].y);
    for (i = 0; i < 6; i++)
        printf("%d ", des[i]);
    printf("\n");
    printf("gd %s %d %d %d %d\n", gd.name, gd.vals[0], gd.vals[1], gd.at.x, gd.at.y);
    for (i = 0; i < 8; i++)
        printf("%d", range[i]);
    printf("\n");
    printf("addr %d %d %d\n", *gip, gpp->y, *gmem);
    printf("gll %llx gsh %d %d %d guc %d %d %d %d\n", gll, gsh[0], gsh[1], gsh[2], guc[0], guc[1],
           guc[2], guc[3]);
    printf("gpad %c %d\n", gpad.c, gpad.i);
    for (i = 0; i < 100; i++)
        s += zeros[i];
    printf("zeros %d\n", s);
    printf("la %d %d %d %d\n", la[0], la[1], la[2], la[3]);
    printf("ls %s %d\n", ls, (int)sizeof ls);
    printf("lp %d %d\n", lp.x, lp.y);
    printf("lr %s %d %d %d\n", lr.name, lr.vals[2], lr.at.x, lr.at.y);
    printf("lm %d %d %d %d %d %d\n", lm[0][0], lm[0][1], lm[0][2], lm[1][0], lm[1][1], lm[1][2]);
    i = counter();
    s = counter();
    printf("static %d %d\n", i, s);
    return 0;
}
