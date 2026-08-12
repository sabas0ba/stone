/* 実物の C で backend を確かめる (その 5)。
 *
 * bzip2 1.0.8 の libbz2 を **無改変で** riscv32-tcc に食わせ，出来た
 * ものを我々の QEMU で走らせる。約 5 千行あり，ビットをいじる処理と
 * 大きな表と再帰が詰まっているので，backend の粗はまず表に出る。
 *
 * 圧縮 -> 伸長 -> 原文と一致，を裸で (libc 無しで) 行う。libbz2 が
 * 要るのは記憶域の割付けと数個の文字列操作だけなので，ここに置く。
 * 期待値は tests/stage015/test.sh にある。
 *
 * Stage 14 では同じ素材を**我々の処理系**でビルドして我々の OS の上で
 * 走らせた (stage014-external.md)。ここは同じ素材を**我々が backend を
 * 書いた tcc** でビルドする。素材が同じなので，両者の出た目を比べられる。 */

typedef unsigned int size_t;

static void putc_(int c) { *(volatile char *)0x10000000 = c; }
static void puthex(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i = i - 4) {
        int d = (v >> i) & 15;
        putc_(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

/* 記憶域は前から配るだけ。返さない */
static char heap[4 * 1024 * 1024];
static unsigned hp = 0;

void *malloc(size_t n)
{
    char *p;
    n = (n + 7) & ~7u;
    if (hp + n > sizeof heap)
        return 0;
    p = heap + hp;
    hp = hp + n;
    return p;
}

void free(void *p) { (void)p; }

void *memcpy(void *d, const void *s, size_t n)
{
    char *a = d; const char *b = s; size_t i;
    for (i = 0; i < n; i++) a[i] = b[i];
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    char *a = d; const char *b = s; size_t i;
    if (a <= b) { for (i = 0; i < n; i++) a[i] = b[i]; }
    else { for (i = n; i > 0; i--) a[i-1] = b[i-1]; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    char *a = d; size_t i;
    for (i = 0; i < n; i++) a[i] = (char)c;
    return d;
}

void exit(int c) { *(volatile int *)0x100000 = 0x5555 | (c << 16); for (;;) ; }

/* BZ_NO_STDIO のときの表明の失敗はこれが受ける */
void bz_internal_error(int errcode)
{
    putc_('E'); puthex((unsigned)errcode); putc_('\n');
    exit(1);
}

int BZ2_bzBuffToBuffCompress(char *dest, unsigned *destLen,
                             char *source, unsigned sourceLen,
                             int blockSize100k, int verbosity, int workFactor);
int BZ2_bzBuffToBuffDecompress(char *dest, unsigned *destLen,
                               char *source, unsigned sourceLen,
                               int small, int verbosity);

#define N 4096
static char src[N];
static char cmp[N * 2];
static char back[N * 2];

void cmain(void)
{
    unsigned clen = sizeof cmp, blen = sizeof back;
    unsigned i, sum = 0;
    int rc;

    /* **.bss は誰も零で埋めない。** QEMU は ELF の載せる部分しか置かず，
       OS もいない。使う前に自分で立てる */
    hp = 0;

    /* 適度に偏りのある原文 (全部同じだと圧縮器の一部しか通らない) */
    for (i = 0; i < N; i++)
        src[i] = (char)('a' + ((i * i + (i >> 3)) % 23));

    rc = BZ2_bzBuffToBuffCompress(cmp, &clen, src, N, 1, 0, 30);
    puthex((unsigned)rc);   putc_(':');
    puthex(clen);           putc_(':');

    for (i = 0; i < clen; i++)
        sum = sum * 31 + (unsigned char)cmp[i];
    puthex(sum);            putc_(':');

    rc = BZ2_bzBuffToBuffDecompress(back, &blen, cmp, clen, 0, 0);
    puthex((unsigned)rc);   putc_(':');
    puthex(blen);           putc_(':');

    for (i = 0; i < N; i++)
        if (back[i] != src[i])
            break;
    puthex(i);              putc_('\n');   /* N なら往復一致 */

    exit(0);
}
