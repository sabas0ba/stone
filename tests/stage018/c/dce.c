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

static int sidefx;

/* 設定の無い時の static inline の代わりの形。GCC は展開して畳む */
extern int missing_table_ref;
static int *const dead_table[] = { &missing_table_ref };

static inline void stub_register(int *const *t)
{
}

static inline int coherent(void)
{
    return 1;
}

static inline int need_uncached(int x)
{
    if (coherent())
        return 0;
    return x;
}

static inline int startup_mode(int x)
{
    return 2;
}

struct inode {
    int i_flags;
};

struct mapping {
    struct inode *host;
};

static inline _Bool dax_mapping(struct mapping *m)
{
    return m->host && (m->host->i_flags & 0);
}

static int count_pages(struct mapping *m, int n)
{
    _Bool dax = dax_mapping(m);
    int i, c = 0;
    for (i = 0; i < n; i++) {
        if (dax) {
            never_defined();
            continue;
        }
        c++;
    }
    if (!0 && need_uncached(n))
        never_defined();
    return c;
}

/* 宣言の後に最上位で 1 度だけ代入する形 */
static int count_pages2(struct mapping *m, int n)
{
    _Bool dax, lock;
    int i, c = 0;
    if (n < 0)
        return -1;
    dax = dax_mapping(m);
    lock = !dax && n > 2;
    for (i = 0; i < n; i++) {
        if (dax)
            never_defined();
        c += lock ? 2 : 1;
    }
    return c;
}

static int bump(void)
{
    return ++sidefx;
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
    /* ICE でない条件も，&& の片側が偽・|| の片側が真なら決まる。左は評価する */
    if (bump() && 0)
        never_defined();
    if (!(bump() || 1))
        never_defined();
    if (bump() && sizeof(int) == 8 && bump())
        never_defined();
    /* 引数に副作用の無い空の関数の呼出しは消え，引数だけが参照する表も出ない */
    stub_register(dead_table);
    if (need_uncached(r))
        never_defined();
    switch (startup_mode(r)) {
    case 1:
        never_defined();
        break;
    case 2:
        r += 100;
        break;
    }
    {
        struct inode ino;
        struct mapping m;
        ino.i_flags = 3;
        m.host = &ino;
        r += count_pages(&m, 5);
        r += count_pages2(&m, 4) * 1000;
    }
    printf("dce %d %d %d %d\n", r, helper(41), size_switch(1), sidefx);
    return 0;
}
