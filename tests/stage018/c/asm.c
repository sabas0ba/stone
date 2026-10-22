/* inline asm (docs/stage018-ncc.md 10 章の M4)。RISC-V の命令を書くので
 * ホストの gcc では組めない。期待は手で書く (tools/ncc18.sh expect は
 * この行の「期待は手で書く」を見て飛ばす)。
 *
 * 64 bit の被演算子: "r" の入力は下位の語のレジスタ，"+rm" と "=m" は
 * 記憶域。jump 擬似命令は nas1 が R_RISCV_CALL で出し，nld1 が解決する。
 * ラベルの直後の asm 文，定数の switch で除かれる 64 bit の出力。 */
int printf(const char *fmt, ...);

static unsigned low_word(unsigned long long v)
{
    unsigned r;
    __asm__("mv %0, %1" : "=r"(r) : "r"(v));
    return r;
}

static unsigned high_word(unsigned long long v)
{
    unsigned r;
    __asm__("mv %0, %z1" : "=r"(r) : "rJ"(v >> 32));
    return r;
}

static unsigned long long keep(unsigned long long v)
{
    __asm__("" : "+rm"(v));
    return v;
}

static void store_low(unsigned long long *p, unsigned w)
{
    __asm__("sw %1, %0" : "=m"(*p) : "r"(w));
}

static int skip(int x)
{
    __asm__("jump 1f, t0\n\tli %0, 99\n1:" : "=r"(x) : "0"(x) : "t0");
    return x;
}

static int labeled(int n)
{
    int i = 0;
retry:
    __asm__ __volatile__("" : : : "memory");
    if (++i < n)
        goto retry;
    return i;
}

static int sized(int *p)
{
    int r = 0;
    switch (sizeof(*p)) {
    case 4:
        __asm__("addi %0, %1, 4" : "=r"(r) : "r"(r));
        break;
    case 8: {
        unsigned long long w;
        __asm__("li %0, 8" : "=r"(w));
        r = (int)w;
        break;
    }
    }
    return r;
}

int main(void)
{
    unsigned long long v = 0x1122334455667788ULL;
    int k = 0;
    printf("low %x high %x\n", low_word(v), high_word(v));
    printf("keep %x %x\n", (unsigned)(keep(v) >> 32), (unsigned)keep(v));
    store_low(&v, 0xabcdef01u);
    printf("store %x %x\n", (unsigned)(v >> 32), (unsigned)v);
    printf("jump %d labeled %d sized %d\n", skip(5), labeled(3), sized(&k));
    return 0;
}
