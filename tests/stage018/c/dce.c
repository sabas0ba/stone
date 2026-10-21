/* 到達しない枝の除去 (docs/stage018-ncc.md 4.3)。どれも定義の無い関数を
 * 呼ぶが，定数の条件で除かれるので結合できる。static の未使用の関数も
 * 出力されないので，その中の未定義の参照も残らない。 */
int printf(const char *fmt, ...);
void never_defined(void);
void bad_size(void);
int also_missing(int);

static void unused_static(void)
{
    never_defined();
}

static inline int helper(int x)
{
    if (sizeof(int) == 8)
        never_defined();
    return x + 1;
}

static int size_switch(int n)
{
    switch (sizeof(long long)) {
    case 4:
        return also_missing(n);
    case 8:
        n += 8;
    case 16:
        n += 16;
        break;
    default:
        bad_size();
    }
    return n;
}

int main(void)
{
    int r = 0;
    if (0)
        never_defined();
    if (sizeof(char) != 1) {
        bad_size();
    } else
        r += 1;
    r += 0 && also_missing(1);
    r += 1 || also_missing(2);
    while (0)
        never_defined();
    do {
        r += 10;
    } while (0);
    printf("dce %d %d %d\n", r, helper(41), size_switch(1));
    return 0;
}
