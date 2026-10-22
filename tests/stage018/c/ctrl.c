/* 制御構造と再帰 */
int printf(const char *fmt, ...);

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int ack(int m, int n)
{
    if (m == 0)
        return n + 1;
    if (n == 0)
        return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}

static const char *kind(int x)
{
    switch (x) {
    case 0:
        return "zero";
    case 1:
    case 2:
        return "small";
    case 3 ... 9:
        return "digit";
    case -1:
        return "minus";
    default:
        return "big";
    }
}

static int fall(int x)
{
    int r = 0;
    switch (x) {
    case 1:
        r += 1;
    case 2:
        r += 10;
        break;
    case 3:
        r += 100;
    default:
        r += 1000;
    }
    return r;
}

int main(void)
{
    int i, j, s = 0;
    for (i = 0; i < 10; i++) {
        if (i == 3)
            continue;
        if (i == 8)
            break;
        s += i;
    }
    printf("for %d\n", s);
    i = 0;
    while (i < 100)
        i += 7;
    printf("while %d\n", i);
    i = 0;
    do {
        i++;
    } while (i < 5);
    printf("do %d\n", i);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            if (j == i)
                continue;
            printf("%d%d ", i, j);
        }
    printf("\n");
    for (i = -2; i < 12; i++)
        printf("%s ", kind(i));
    printf("\n");
    printf("fall %d %d %d %d\n", fall(1), fall(2), fall(3), fall(4));
    printf("fib %d ack %d\n", fib(20), ack(2, 3));
    i = 0;
again:
    i++;
    if (i < 10)
        goto again;
    printf("goto %d\n", i);
    s = 0;
    for (i = 0; i < 20; i++)
        s += i % 3 == 0 ? i : i % 3 == 1 ? -i : 0;
    printf("cond %d\n", s);
    printf("logic %d %d %d %d\n", i > 3 && i < 100, i < 3 || i > 100, !(i && 0), (i || 0));
    return 0;
}
