/* ポインタ・配列・関数へのポインタ・文字列 */
int printf(const char *fmt, ...);

int arr[10];
int grid[3][4];
const char *names[] = { "alpha", "beta", "gamma" };

static int sq(int x) { return x * x; }
static int neg(int x) { return -x; }
static int (*ops[2])(int) = { sq, neg };

static void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

static int sum(const int *p, int n)
{
    int s = 0;
    while (n-- > 0)
        s += *p++;
    return s;
}

static int slen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (int)(p - s);
}

int main(void)
{
    int i, j, x = 3, y = 4, *p;
    char buf[16];
    int (*fp)(int) = sq;
    for (i = 0; i < 10; i++)
        arr[i] = i * i;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            grid[i][j] = i * 10 + j;
    printf("arr %d %d %d\n", arr[3], *(arr + 5), sum(arr, 10));
    printf("grid %d %d %d\n", grid[2][3], *(*(grid + 1) + 2), (int)(&grid[2][0] - &grid[0][0]));
    swap(&x, &y);
    printf("swap %d %d\n", x, y);
    p = &arr[9];
    printf("pdiff %d %d\n", (int)(p - arr), (int)(&arr[2] - p));
    p -= 3;
    printf("parith %d %d\n", *p, p[-1]);
    printf("names %s %s %d\n", names[1], names[2] + 2, slen(names[0]));
    printf("fp %d %d %d\n", fp(7), ops[0](5), ops[1](5));
    for (i = 0; i < 5; i++)
        buf[i] = (char)('a' + i);
    buf[5] = 0;
    printf("buf %s %c\n", buf, buf[2]);
    printf("cmp %d %d\n", p > arr, p == &arr[6]);
    {
        int *q = 0;
        printf("null %d %d\n", q == 0, !q);
    }
    {
        char *s = "string literal";
        printf("lit %c %s\n", s[7], s + 7);
    }
    printf("sizeof %d %d %d\n", (int)sizeof arr, (int)sizeof grid[0], (int)sizeof(names) / (int)sizeof(names[0]));
    return 0;
}
