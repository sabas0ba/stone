/* nas1.c --- RV32IMA + Zicsr + Zifencei のアセンブラ (docs/stage018-ncc.md 6.1)
 *
 *   nas1 [-o <出力>] [-I <dir>]... [<入力>]
 *
 * GNU as の書式の部分集合を読み，ELF32 の再配置可能オブジェクト (ET_REL) を
 * 出す。入力を省くと標準入力を，-o を省くと標準出力を使う。-march= / -mabi= /
 * -mno-relax などの選択肢は受けて無視する。
 *
 * 2 パスで組み立てる。緩和をせず圧縮命令も無いので，命令の大きさは 1 パス目で
 * 決まる。1 パス目でラベルの番地を決め，2 パス目でバイト列と再配置を作る。
 * 前方の値に依る `li` は 1 パス目の判断 (2 命令) を 2 パス目でも使う。
 *
 * 再配置の方針:
 *   - 分岐と jal は，同じ節の weak でない記号ならその場で解決する
 *   - call / tail は常に R_RISCV_CALL_PLT を出す
 *   - %pcrel_hi / %pcrel_lo は常に再配置にする。%pcrel_lo は auipc のラベルを
 *     指すので，そのラベルは .L で始まっても記号表に載せる
 *   - 局所の記号への再配置は節の記号 + 位置に置き換える
 *   - R_RISCV_RELAX は出さない
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む (同 5.1)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ELF の定数 ---- */
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_PREINIT_ARRAY 16
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_MERGE 0x10
#define SHF_STRINGS 0x20
#define SHF_INFO_LINK 0x40
#define SHF_TLS 0x400
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STV_HIDDEN 2
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2

#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_BRANCH 16
#define R_RISCV_JAL 17
#define R_RISCV_CALL_PLT 19
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
#define R_RISCV_LO12_S 28
#define R_RISCV_ADD8 33
#define R_RISCV_ADD16 34
#define R_RISCV_ADD32 35
#define R_RISCV_ADD64 36
#define R_RISCV_SUB8 37
#define R_RISCV_SUB16 38
#define R_RISCV_SUB32 39
#define R_RISCV_SUB64 40

/* 記号の置き場所 (sym.sec)。0 以上は節の番号 */
#define S_UNDEF (-1)
#define S_ABS (-2)
#define S_COMMON (-3)
#define S_ALIAS (-4)

/* 式の修飾子 */
#define M_NONE 0
#define M_HI 1
#define M_LO 2
#define M_PCREL_HI 3
#define M_PCREL_LO 4

#define MAXSEC 512
#define MAXSTACK 64
#define MAXMACARG 32
#define MAXLINE 8192

struct sym {
    char *name;
    struct sym *next;
    int sec;            /* S_* または節の番号 */
    long long val;      /* 節の中の位置，S_ABS なら値，S_COMMON なら整列 */
    struct sym *alias;  /* S_ALIAS: val を足す相手 */
    int bind;
    int type;
    int hidden;
    long long size;
    int has_size;
    int defpass;        /* 定義したパス (0 は未定義) */
    int used;           /* 再配置が参照した */
    int keep;           /* .L でも記号表に載せる */
    int elfidx;
};

struct rel {
    long off;
    int type;
    struct sym *sym;
    long long addend;
};

struct sec {
    char *name;
    int type;
    unsigned long flags;
    long align;
    long entsize;
    unsigned char *data;
    long size;
    long cap;
    struct rel *rel;
    int nrel;
    int crel;
    struct sym *ssym;   /* 節の記号 */
    int elfidx;
    int relidx;
    long nameoff;
    long relnameoff;
};

struct val {
    long long c;
    struct sym *s;      /* 足す記号 (無ければ 0) */
    struct sym *sub;    /* 引く記号。データの ADD / SUB の対でだけ使う */
    int mod;
    int unk;            /* 1 パス目でまだ値が分からない */
};

struct macro {
    char *name;
    int nparam;
    char *param[MAXMACARG];
    char *defval[MAXMACARG];
    int nline;
    char **line;
    struct macro *next;
};

struct numlab {
    long n;
    int cnt;
    struct numlab *next;
};

/* ---- 状態 ---- */
static int pass;
static long lineno;
static const char *infile = "<stdin>";
static struct sec secs[MAXSEC];
static int nsec;
static int cur;
static int prevsec;
static int secstack[MAXSTACK];
static int prevstack[MAXSTACK];
static int nsecstack;
static struct sym *symtab[4096];
static struct macro *macros;
static struct numlab *numlabs;
static int altmacro;
static long localcnt;
static long pcrelcnt;
static long licnt;
static unsigned char *litwo;    /* li の判断 (1 パス目の結果) */
static long litwocap;
static int ifstack[MAXSTACK];   /* 1: 有効 */
static int ifdone[MAXSTACK];
static int nif;
static int macdepth;
static char *incdirs[64];
static int nincdir;

/* ---- 補助 ---- */
static void fatal(const char *msg, const char *arg)
{
    if (arg)
        fprintf(stderr, "nas1: %s:%ld: %s: %s\n", infile, lineno, msg, arg);
    else
        fprintf(stderr, "nas1: %s:%ld: %s\n", infile, lineno, msg);
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p)
        fatal("記憶域が足りない", 0);
    return p;
}

static void *xrealloc(void *p, long n)
{
    p = realloc(p, n > 0 ? n : 1);
    if (!p)
        fatal("記憶域が足りない", 0);
    return p;
}

static char *xstrndup(const char *s, long n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *xstrdup(const char *s)
{
    return xstrndup(s, (long)strlen(s));
}

static int isid0(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
        || c == '.' || c == '$';
}

static int isid(int c)
{
    return isid0(c) || (c >= '0' && c <= '9');
}

static int isdig(int c)
{
    return c >= '0' && c <= '9';
}

static int issp(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

static const char *skipsp(const char *p)
{
    while (issp(*p))
        p++;
    return p;
}

/* ---- 記号表 ---- */
static unsigned long hashstr(const char *s)
{
    unsigned long h = 2166136261UL;
    while (*s) {
        h = (h ^ (unsigned char)*s++) * 16777619UL;
        h &= 0xffffffffUL;
    }
    return h;
}

static struct sym *lookup(const char *name, int create)
{
    unsigned long h = hashstr(name) % 4096;
    struct sym *s;
    for (s = symtab[h]; s; s = s->next)
        if (strcmp(s->name, name) == 0)
            return s;
    if (!create)
        return 0;
    s = xmalloc(sizeof(struct sym));
    memset(s, 0, sizeof(struct sym));
    s->name = xstrdup(name);
    s->sec = S_UNDEF;
    s->next = symtab[h];
    symtab[h] = s;
    return s;
}

static int islocalname(const char *n)
{
    return n[0] == '.' && n[1] == 'L';
}

/* 記号をこのパスで定義する */
static void defsym(struct sym *s, int sec, long long val)
{
    if (s->defpass == pass && s->sec != S_ABS && s->sec != S_ALIAS)
        fatal("記号の二重定義", s->name);
    s->sec = sec;
    s->val = val;
    s->alias = 0;
    s->defpass = pass;
}

/* ---- 節 ---- */
static void secgrow(struct sec *s, long need)
{
    if (s->size + need <= s->cap)
        return;
    while (s->cap < s->size + need)
        s->cap = s->cap ? s->cap * 2 : 4096;
    s->data = xrealloc(s->data, s->cap);
}

static int startswith(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* 名前で決まる既定の型と旗 (GNU as の文書: 認めない名前は旗なし) */
static void secdefaults(const char *n, int *type, unsigned long *flags)
{
    *type = SHT_PROGBITS;
    *flags = 0;
    if (strcmp(n, ".text") == 0 || startswith(n, ".text.")
        || strcmp(n, ".init") == 0 || strcmp(n, ".fini") == 0)
        *flags = SHF_ALLOC | SHF_EXECINSTR;
    else if (strcmp(n, ".data") == 0 || startswith(n, ".data.")
             || strcmp(n, ".sdata") == 0 || startswith(n, ".sdata."))
        *flags = SHF_ALLOC | SHF_WRITE;
    else if (strcmp(n, ".bss") == 0 || startswith(n, ".bss.")
             || strcmp(n, ".sbss") == 0 || startswith(n, ".sbss.")) {
        *flags = SHF_ALLOC | SHF_WRITE;
        *type = SHT_NOBITS;
    } else if (strcmp(n, ".rodata") == 0 || startswith(n, ".rodata.")
               || strcmp(n, ".srodata") == 0 || startswith(n, ".srodata."))
        *flags = SHF_ALLOC;
    else if (strcmp(n, ".init_array") == 0 || startswith(n, ".init_array.")) {
        *flags = SHF_ALLOC | SHF_WRITE;
        *type = SHT_INIT_ARRAY;
    } else if (strcmp(n, ".fini_array") == 0 || startswith(n, ".fini_array.")) {
        *flags = SHF_ALLOC | SHF_WRITE;
        *type = SHT_FINI_ARRAY;
    } else if (strcmp(n, ".preinit_array") == 0) {
        *flags = SHF_ALLOC | SHF_WRITE;
        *type = SHT_PREINIT_ARRAY;
    } else if (startswith(n, ".note"))
        *type = SHT_NOTE;
}

static int findsec(const char *name)
{
    int i;
    for (i = 0; i < nsec; i++)
        if (strcmp(secs[i].name, name) == 0)
            return i;
    return -1;
}

/* 節を引く。無ければ作る。旗の指定があれば (has) それで決める */
static int getsec(const char *name, int has, int type, unsigned long flags, long entsize)
{
    int i = findsec(name);
    struct sec *s;
    if (i >= 0)
        return i;
    if (nsec >= MAXSEC)
        fatal("節が多すぎる", name);
    s = &secs[nsec];
    memset(s, 0, sizeof(struct sec));
    s->name = xstrdup(name);
    if (has) {
        s->type = type;
        s->flags = flags;
        s->entsize = entsize;
    } else
        secdefaults(name, &s->type, &s->flags);
    s->align = 1;
    s->ssym = xmalloc(sizeof(struct sym));
    memset(s->ssym, 0, sizeof(struct sym));
    s->ssym->name = s->name;
    s->ssym->sec = nsec;
    s->ssym->type = STT_SECTION;
    s->ssym->defpass = 1;
    return nsec++;
}

static void setsec(int i)
{
    prevsec = cur;
    cur = i;
}

static long here(void)
{
    return secs[cur].size;
}

static void emitbyte(int b)
{
    struct sec *s = &secs[cur];
    if (s->type == SHT_NOBITS) {
        if (b != 0 && pass == 2)
            fatal("nobits の節に 0 以外を置いた", s->name);
        s->size++;
        return;
    }
    secgrow(s, 1);
    s->data[s->size++] = (unsigned char)b;
}

static void emitn(unsigned long long v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        emitbyte((int)(v & 0xff));
        v >>= 8;
    }
}

static void emit32(unsigned long v)
{
    emitn((unsigned long long)(v & 0xffffffffUL), 4);
}

static void addrel(long off, int type, struct sym *s, long long addend)
{
    struct sec *sc = &secs[cur];
    struct rel *r;
    if (pass != 2)
        return;
    /* 局所の記号は節の記号 + 位置に置き換える (%pcrel_lo の相手を除く) */
    while (s->sec == S_ALIAS && s->alias) {
        addend += s->val;
        s = s->alias;
    }
    if (s->bind == STB_LOCAL && s->sec >= 0 && s->type != STT_SECTION
        && type != R_RISCV_PCREL_LO12_I && type != R_RISCV_PCREL_LO12_S) {
        addend += s->val;
        s = secs[s->sec].ssym;
    }
    if (type == R_RISCV_PCREL_LO12_I || type == R_RISCV_PCREL_LO12_S)
        s->keep = 1;
    s->used = 1;
    if (sc->nrel >= sc->crel) {
        sc->crel = sc->crel ? sc->crel * 2 : 64;
        sc->rel = xrealloc(sc->rel, (long)sc->crel * (long)sizeof(struct rel));
    }
    r = &sc->rel[sc->nrel++];
    r->off = off;
    r->type = type;
    r->sym = s;
    r->addend = addend;
}

/* ---- 数字のラベル ---- */
static struct numlab *numlab(long n)
{
    struct numlab *p;
    for (p = numlabs; p; p = p->next)
        if (p->n == n)
            return p;
    p = xmalloc(sizeof(struct numlab));
    p->n = n;
    p->cnt = 0;
    p->next = numlabs;
    numlabs = p;
    return p;
}

static struct sym *numsym(long n, int k)
{
    char buf[64];
    sprintf(buf, ".Ln%ld_%d", n, k);
    return lookup(buf, 1);
}

/* ---- 式 ---- */
static const char *ep;

static void expr(struct val *v);

/* 記号を値へ解く。S_ABS は定数に，S_ALIAS は相手に辿る */
static void symval(struct sym *s, struct val *v)
{
    int depth = 0;
    v->c = 0;
    v->s = 0;
    v->sub = 0;
    v->mod = M_NONE;
    v->unk = 0;
    while (s->sec == S_ALIAS && s->alias && depth < 100) {
        v->c += s->val;
        s = s->alias;
        depth++;
    }
    if (s->sec == S_ABS) {
        v->c += s->val;
        return;
    }
    v->s = s;
    /* 1 パス目でまだ定義を見ていない記号は，後ろの .set の定数かもしれない */
    if (pass == 1 && s->defpass == 0)
        v->unk = 1;
}

/* 記号の (節, 位置) が分かれば 1 */
static int symloc(struct sym *s, int *sec, long long *off)
{
    if (s->sec >= 0 && (s->defpass > 0 || s->type == STT_SECTION)) {
        *sec = s->sec;
        *off = s->type == STT_SECTION ? 0 : s->val;
        return 1;
    }
    return 0;
}

static long long parsenum(void)
{
    long long v = 0;
    int base = 10;
    if (ep[0] == '0' && (ep[1] == 'x' || ep[1] == 'X')) {
        base = 16;
        ep += 2;
    } else if (ep[0] == '0' && (ep[1] == 'b' || ep[1] == 'B')
               && (ep[2] == '0' || ep[2] == '1')) {
        base = 2;
        ep += 2;
    } else if (ep[0] == '0' && isdig(ep[1]))
        base = 8;
    for (;;) {
        int c = *ep, d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
        ep++;
    }
    return v;
}

static int parseesc(void)
{
    int c = *ep++;
    if (c != '\\')
        return c;
    c = *ep++;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    case 'a': return 7;
    case 'x': {
        int v = 0, d;
        for (;;) {
            c = *ep;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = v * 16 + d;
            ep++;
        }
        return v & 0xff;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = c - '0', i;
            for (i = 0; i < 2 && *ep >= '0' && *ep <= '7'; i++)
                v = v * 8 + (*ep++ - '0');
            return v & 0xff;
        }
        return c;
    }
}

static void setconst(struct val *v, long long c)
{
    v->c = c;
    v->s = 0;
    v->sub = 0;
    v->mod = M_NONE;
    v->unk = 0;
}

static void primary(struct val *v)
{
    ep = skipsp(ep);
    if (*ep == '(') {
        ep++;
        expr(v);
        ep = skipsp(ep);
        if (*ep != ')')
            fatal("')' が無い", ep);
        ep++;
        return;
    }
    if (*ep == '-' || *ep == '~' || *ep == '!' || *ep == '+') {
        int op = *ep++;
        primary(v);
        if (op == '+')
            return;
        if (v->s || v->mod) {
            if (v->unk || pass == 1) {
                v->unk = 1;
                v->s = 0;
                return;
            }
            fatal("記号に単項演算子を使った", 0);
        }
        if (op == '-')
            v->c = -v->c;
        else if (op == '~')
            v->c = ~v->c;
        else
            v->c = !v->c;
        return;
    }
    if (*ep == '%') {
        static const char *mods[] = { "hi", "lo", "pcrel_hi", "pcrel_lo", 0 };
        int i, m = 0;
        const char *q = ep + 1;
        for (i = 0; mods[i]; i++) {
            long n = (long)strlen(mods[i]);
            if (strncmp(q, mods[i], n) == 0 && skipsp(q + n)[0] == '(') {
                m = i + 1;
                ep = skipsp(q + n) + 1;
                break;
            }
        }
        if (!m)
            fatal("知らない修飾子", ep);
        expr(v);
        ep = skipsp(ep);
        if (*ep != ')')
            fatal("')' が無い", ep);
        ep++;
        if (v->mod)
            fatal("修飾子を重ねた", 0);
        v->mod = m;
        return;
    }
    if (*ep == '\'') {
        ep++;
        setconst(v, parseesc());
        if (*ep == '\'')
            ep++;
        return;
    }
    if (isdig(*ep)) {
        const char *q = ep;
        while (isdig(*q))
            q++;
        if ((*q == 'b' || *q == 'f') && !isid(q[1])) {
            long n = strtol(ep, 0, 10);
            struct numlab *nl = numlab(n);
            int k = *q == 'b' ? nl->cnt - 1 : nl->cnt;
            ep = q + 1;
            if (k < 0)
                fatal("後方の数字ラベルが無い", 0);
            symval(numsym(n, k), v);
            if (v->s && v->s->defpass != pass && *q == 'f')
                v->unk = pass == 1;
            return;
        }
        setconst(v, parsenum());
        return;
    }
    if (isid0(*ep)) {
        const char *q = ep;
        char *name;
        struct sym *s;
        while (isid(*q))
            q++;
        if (q - ep == 1 && *ep == '.') {
            ep = q;
            v->c = here();
            v->s = secs[cur].ssym;
            v->sub = 0;
            v->mod = M_NONE;
            v->unk = 0;
            return;
        }
        name = xstrndup(ep, q - ep);
        ep = q;
        s = lookup(name, 1);
        free(name);
        symval(s, v);
        return;
    }
    fatal("式が読めない", ep);
}

/* 2 項演算。記号を含む場合は + と - だけを許す */
static void binop(struct val *a, int op, struct val *b)
{
    if (a->mod || b->mod)
        fatal("修飾子の付いた値に演算をした", 0);
    if (a->unk || b->unk) {
        a->unk = 1;
        a->s = 0;
        a->sub = 0;
        return;
    }
    if (b->sub || (a->sub && op != '+' && op != '-') || (a->sub && b->s))
        fatal("記号の差にさらに記号の演算をした", 0);
    if (op == '+') {
        if (a->s && b->s)
            fatal("記号同士を足した", 0);
        if (b->s)
            a->s = b->s;
        a->c += b->c;
        return;
    }
    if (op == '-') {
        if (b->s) {
            int sa, sb;
            long long oa, ob;
            if (!a->s)
                fatal("定数から記号を引いた", 0);
            if (symloc(a->s, &sa, &oa) && symloc(b->s, &sb, &ob) && sa == sb) {
                a->c = a->c + oa - b->c - ob;
                a->s = 0;
                return;
            }
            if (pass == 1) {
                a->unk = 1;
                a->s = 0;
                return;
            }
            /* 解けない差はデータの ADD / SUB の再配置の対にする */
            a->sub = b->s;
            a->c -= b->c;
            return;
        }
        a->c -= b->c;
        return;
    }
    if (a->s || b->s) {
        if (pass == 1) {
            a->unk = 1;
            a->s = 0;
            return;
        }
        fatal("記号に + / - 以外の演算をした", 0);
    }
    switch (op) {
    case '*': a->c *= b->c; break;
    case '/': if (!b->c) fatal("0 で割った", 0); a->c /= b->c; break;
    case '%': if (!b->c) fatal("0 で割った", 0); a->c %= b->c; break;
    case 'L': a->c = (long long)((unsigned long long)a->c << (b->c & 63)); break;
    case 'R': a->c >>= (b->c & 63); break;
    case '|': a->c |= b->c; break;
    case '&': a->c &= b->c; break;
    case '^': a->c ^= b->c; break;
    case '!': a->c |= ~b->c; break;
    case '=': a->c = a->c == b->c ? -1 : 0; break;
    case 'N': a->c = a->c != b->c ? -1 : 0; break;
    case '<': a->c = a->c < b->c ? -1 : 0; break;
    case '>': a->c = a->c > b->c ? -1 : 0; break;
    case 'l': a->c = a->c <= b->c ? -1 : 0; break;
    case 'g': a->c = a->c >= b->c ? -1 : 0; break;
    case 'A': a->c = (a->c && b->c) ? 1 : 0; break;
    case 'O': a->c = (a->c || b->c) ? 1 : 0; break;
    }
}

/* 演算子を読む。level: 3 (* / % << >>)，2 (| & ^ !)，1 (+ - と比較)，0 (&& ||) */
static int peekop(int level, int *len)
{
    const char *p = skipsp(ep);
    *len = 1;
    switch (level) {
    case 3:
        if (*p == '*' || *p == '/' || *p == '%')
            return *p;
        if (p[0] == '<' && p[1] == '<') { *len = 2; return 'L'; }
        if (p[0] == '>' && p[1] == '>') { *len = 2; return 'R'; }
        return 0;
    case 2:
        if (*p == '|' && p[1] != '|')
            return '|';
        if (*p == '&' && p[1] != '&')
            return '&';
        if (*p == '^')
            return '^';
        if (*p == '!' && p[1] != '=')
            return '!';
        return 0;
    case 1:
        if (*p == '+' || *p == '-')
            return *p;
        if (p[0] == '=' && p[1] == '=') { *len = 2; return '='; }
        if (p[0] == '!' && p[1] == '=') { *len = 2; return 'N'; }
        if (p[0] == '<' && p[1] == '>') { *len = 2; return 'N'; }
        if (p[0] == '<' && p[1] == '=') { *len = 2; return 'l'; }
        if (p[0] == '>' && p[1] == '=') { *len = 2; return 'g'; }
        if (p[0] == '<' && p[1] != '<') return '<';
        if (p[0] == '>' && p[1] != '>') return '>';
        return 0;
    default:
        if (p[0] == '&' && p[1] == '&') { *len = 2; return 'A'; }
        if (p[0] == '|' && p[1] == '|') { *len = 2; return 'O'; }
        return 0;
    }
}

static void exprlevel(struct val *v, int level)
{
    int op, len;
    struct val b;
    if (level > 3) {
        primary(v);
        return;
    }
    exprlevel(v, level + 1);
    while ((op = peekop(level, &len)) != 0) {
        ep = skipsp(ep) + len;
        exprlevel(&b, level + 1);
        binop(v, op, &b);
    }
}

static void expr(struct val *v)
{
    exprlevel(v, 0);
}

/* 文字列 s の全体を式として読む */
static void evalstr(const char *s, struct val *v)
{
    ep = s;
    expr(v);
    ep = skipsp(ep);
    if (*ep)
        fatal("式の後に余分なものがある", ep);
}

/* 記号の差 (sub) を許さない文脈 */
static void nosub(struct val *v, const char *s)
{
    if (v->sub)
        fatal("別の節または未定義の記号の差はここに書けない", s);
}

/* 定数でなければならない式 */
static long long evalconst(const char *s, int *unk)
{
    struct val v;
    evalstr(s, &v);
    nosub(&v, s);
    if (v.mod)
        fatal("ここに修飾子は使えない", s);
    if (v.unk) {
        if (!unk)
            fatal("値が決まらない", s);
        *unk = 1;
        return 0;
    }
    if (v.s) {
        if (pass == 1 && unk) {
            *unk = 1;
            return 0;
        }
        fatal("定数でない", s);
    }
    if (unk)
        *unk = 0;
    return v.c;
}

/* ---- 被演算子の分割 ---- */
static int splitargs(char *s, char **out, int max)
{
    int n = 0, depth = 0, instr = 0;
    char *p = s, *start;
    s = (char *)skipsp(s);
    if (!*s)
        return 0;
    start = s;
    for (p = s; ; p++) {
        if (instr) {
            if (*p == '\\' && p[1])
                p++;
            else if (*p == '"')
                instr = 0;
            continue;
        }
        if (*p == '"')
            instr = 1;
        else if (*p == '(')
            depth++;
        else if (*p == ')')
            depth--;
        else if ((*p == ',' && depth == 0) || *p == 0) {
            char *e = p;
            int last = *p == 0;
            while (e > start && issp(e[-1]))
                e--;
            *e = 0;
            if (n >= max)
                fatal("被演算子が多すぎる", 0);
            out[n++] = (char *)skipsp(start);
            if (last)
                break;
            start = p + 1;
        }
    }
    return n;
}

/* ---- レジスタ ---- */
static const char *abinames[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static int regno(const char *s)
{
    int i;
    if (s[0] == 'x' && isdig(s[1])) {
        char *e;
        long n = strtol(s + 1, &e, 10);
        if (*e == 0 && n >= 0 && n < 32)
            return (int)n;
        return -1;
    }
    if (strcmp(s, "fp") == 0)
        return 8;
    for (i = 0; i < 32; i++)
        if (strcmp(s, abinames[i]) == 0)
            return i;
    return -1;
}

static int reg(const char *s)
{
    int r = regno(s);
    if (r < 0)
        fatal("レジスタでない", s);
    return r;
}

/* ---- CSR ---- */
struct csrname {
    const char *name;
    int num;
};

static const struct csrname csrs[] = {
    { "fflags", 0x001 }, { "frm", 0x002 }, { "fcsr", 0x003 },
    { "cycle", 0xc00 }, { "time", 0xc01 }, { "instret", 0xc02 },
    { "cycleh", 0xc80 }, { "timeh", 0xc81 }, { "instreth", 0xc82 },
    { "sstatus", 0x100 }, { "sie", 0x104 }, { "stvec", 0x105 },
    { "scounteren", 0x106 }, { "sscratch", 0x140 }, { "sepc", 0x141 },
    { "scause", 0x142 }, { "stval", 0x143 }, { "sbadaddr", 0x143 },
    { "sip", 0x144 }, { "satp", 0x180 }, { "sptbr", 0x180 },
    { "mstatus", 0x300 }, { "misa", 0x301 }, { "medeleg", 0x302 },
    { "mideleg", 0x303 }, { "mie", 0x304 }, { "mtvec", 0x305 },
    { "mcounteren", 0x306 }, { "mstatush", 0x310 },
    { "mscratch", 0x340 }, { "mepc", 0x341 }, { "mcause", 0x342 },
    { "mtval", 0x343 }, { "mbadaddr", 0x343 }, { "mip", 0x344 },
    { "pmpcfg0", 0x3a0 }, { "pmpcfg1", 0x3a1 }, { "pmpcfg2", 0x3a2 },
    { "pmpcfg3", 0x3a3 },
    { "pmpaddr0", 0x3b0 }, { "pmpaddr1", 0x3b1 }, { "pmpaddr2", 0x3b2 },
    { "pmpaddr3", 0x3b3 }, { "pmpaddr4", 0x3b4 }, { "pmpaddr5", 0x3b5 },
    { "pmpaddr6", 0x3b6 }, { "pmpaddr7", 0x3b7 }, { "pmpaddr8", 0x3b8 },
    { "pmpaddr9", 0x3b9 }, { "pmpaddr10", 0x3ba }, { "pmpaddr11", 0x3bb },
    { "pmpaddr12", 0x3bc }, { "pmpaddr13", 0x3bd }, { "pmpaddr14", 0x3be },
    { "pmpaddr15", 0x3bf },
    { "mcycle", 0xb00 }, { "minstret", 0xb02 }, { "mcycleh", 0xb80 },
    { "minstreth", 0xb82 },
    { "mvendorid", 0xf11 }, { "marchid", 0xf12 }, { "mimpid", 0xf13 },
    { "mhartid", 0xf14 },
    { 0, 0 }
};

static int csrno(const char *s)
{
    int i;
    for (i = 0; csrs[i].name; i++)
        if (strcmp(s, csrs[i].name) == 0)
            return csrs[i].num;
    {
        long long v = evalconst(s, 0);
        if (v < 0 || v > 0xfff)
            fatal("CSR の番号が範囲外", s);
        return (int)v;
    }
}

/* ---- 命令の形 ---- */
static unsigned long enc_r(int op, int f3, int f7, int rd, int rs1, int rs2)
{
    return ((unsigned long)f7 << 25) | ((unsigned long)rs2 << 20)
        | ((unsigned long)rs1 << 15) | ((unsigned long)f3 << 12)
        | ((unsigned long)rd << 7) | (unsigned long)op;
}

static unsigned long enc_i(int op, int f3, int rd, int rs1, long imm)
{
    return (((unsigned long)imm & 0xfff) << 20) | ((unsigned long)rs1 << 15)
        | ((unsigned long)f3 << 12) | ((unsigned long)rd << 7) | (unsigned long)op;
}

static unsigned long enc_s(int op, int f3, int rs1, int rs2, long imm)
{
    unsigned long u = (unsigned long)imm & 0xfff;
    return ((u >> 5) << 25) | ((unsigned long)rs2 << 20) | ((unsigned long)rs1 << 15)
        | ((unsigned long)f3 << 12) | ((u & 0x1f) << 7) | (unsigned long)op;
}

static unsigned long enc_b(int op, int f3, int rs1, int rs2, long imm)
{
    unsigned long u = (unsigned long)imm & 0x1fff;
    return (((u >> 12) & 1) << 31) | (((u >> 5) & 0x3f) << 25)
        | ((unsigned long)rs2 << 20) | ((unsigned long)rs1 << 15)
        | ((unsigned long)f3 << 12) | (((u >> 1) & 0xf) << 8)
        | (((u >> 11) & 1) << 7) | (unsigned long)op;
}

static unsigned long enc_u(int op, int rd, long imm20)
{
    return (((unsigned long)imm20 & 0xfffff) << 12) | ((unsigned long)rd << 7)
        | (unsigned long)op;
}

static unsigned long enc_j(int op, int rd, long imm)
{
    unsigned long u = (unsigned long)imm & 0x1fffff;
    return (((u >> 20) & 1) << 31) | (((u >> 1) & 0x3ff) << 21)
        | (((u >> 11) & 1) << 20) | (((u >> 12) & 0xff) << 12)
        | ((unsigned long)rd << 7) | (unsigned long)op;
}

static void chkimm(long long v, long lo, long hi, const char *what)
{
    if (v < lo || v > hi)
        fatal("即値が範囲外", what);
}

/* 12 bit の符号付き即値 (I / S 形)。%lo / %pcrel_lo なら再配置 */
static long immI(const char *s, int stype, long insnoff)
{
    struct val v;
    evalstr(s, &v);
    nosub(&v, s);
    if (v.mod == M_LO) {
        if (pass == 2) {
            if (!v.s)
                return (long)(((v.c & 0xfff) ^ 0x800) - 0x800);
            addrel(insnoff, stype ? R_RISCV_LO12_S : R_RISCV_LO12_I, v.s, v.c);
        }
        return 0;
    }
    if (v.mod == M_PCREL_LO) {
        if (pass == 2) {
            if (!v.s || v.c)
                fatal("%pcrel_lo にはラベルだけを書く", s);
            addrel(insnoff, stype ? R_RISCV_PCREL_LO12_S : R_RISCV_PCREL_LO12_I, v.s, 0);
        }
        return 0;
    }
    if (v.mod)
        fatal("ここに使えない修飾子", s);
    if (v.unk && pass == 1)
        return 0;
    if (v.s)
        fatal("即値が定数でない", s);
    chkimm(v.c, -2048, 2047, s);
    return (long)v.c;
}

/* 20 bit の上位即値 (lui / auipc) */
static long immU(const char *s, long insnoff, int pcrel)
{
    struct val v;
    evalstr(s, &v);
    nosub(&v, s);
    if (v.mod == M_HI && !pcrel) {
        if (pass == 2) {
            if (!v.s)
                return (long)(((v.c + 0x800) >> 12) & 0xfffff);
            addrel(insnoff, R_RISCV_HI20, v.s, v.c);
        }
        return 0;
    }
    if (v.mod == M_PCREL_HI && pcrel) {
        if (pass == 2) {
            if (!v.s)
                fatal("%pcrel_hi に定数を書いた", s);
            addrel(insnoff, R_RISCV_PCREL_HI20, v.s, v.c);
        }
        return 0;
    }
    if (v.mod)
        fatal("ここに使えない修飾子", s);
    if (v.unk && pass == 1)
        return 0;
    if (v.s)
        fatal("即値が定数でない", s);
    chkimm(v.c, 0, 0xfffff, s);
    return (long)v.c;
}

/* 分岐と jal の行き先。同じ節の weak でない記号はその場で解く */
static long target(const char *s, long insnoff, int rtype)
{
    struct val v;
    int sec;
    long long off;
    evalstr(s, &v);
    nosub(&v, s);
    if (v.mod)
        fatal("行き先に修飾子は使えない", s);
    if (pass == 1)
        return 0;
    if (!v.s)
        fatal("行き先が定数である (絶対番地への分岐は持たない)", s);
    if (v.s->bind != STB_WEAK && symloc(v.s, &sec, &off) && sec == cur) {
        long long d = off + v.c - insnoff;
        if (rtype == R_RISCV_BRANCH)
            chkimm(d, -4096, 4094, s);
        else
            chkimm(d, -1048576, 1048574, s);
        if (d & 1)
            fatal("行き先が 2 バイト境界にない", s);
        return (long)d;
    }
    addrel(insnoff, rtype, v.s, v.c);
    return 0;
}

/* 番地の被演算子 off(reg) を分ける。off は空でもよい */
static void memop(char *s, char **off, int *base)
{
    long n = (long)strlen(s);
    long i;
    int depth = 0;
    char *r;
    if (n == 0 || s[n - 1] != ')')
        fatal("番地の書式でない", s);
    for (i = n - 1; i >= 0; i--) {
        if (s[i] == ')')
            depth++;
        else if (s[i] == '(') {
            depth--;
            if (depth == 0)
                break;
        }
    }
    if (i < 0)
        fatal("括弧が合わない", s);
    s[n - 1] = 0;
    r = (char *)skipsp(s + i + 1);
    {
        char *e = r + strlen(r);
        while (e > r && issp(e[-1]))
            *--e = 0;
    }
    *base = reg(r);
    s[i] = 0;
    *off = (char *)skipsp(s);
}

/* ---- 命令表 ---- */
#define K_R 1       /* rd, rs1, rs2 */
#define K_I 2       /* rd, rs1, imm */
#define K_SH 3      /* rd, rs1, shamt */
#define K_LD 4      /* rd, off(rs1) */
#define K_ST 5      /* rs2, off(rs1) */
#define K_BR 6      /* rs1, rs2, target */
#define K_LUI 7
#define K_AUIPC 8
#define K_SYS 9     /* 被演算子なし。値は固定 */
#define K_CSR 10    /* rd, csr, rs1 */
#define K_CSRI 11   /* rd, csr, uimm */
#define K_AMO 12    /* rd, rs2, (rs1) */
#define K_LR 13     /* rd, (rs1) */

struct insn {
    const char *name;
    int kind;
    int op;
    int f3;
    int f7;
};

static const struct insn insns[] = {
    { "add", K_R, 0x33, 0, 0x00 }, { "sub", K_R, 0x33, 0, 0x20 },
    { "sll", K_R, 0x33, 1, 0x00 }, { "slt", K_R, 0x33, 2, 0x00 },
    { "sltu", K_R, 0x33, 3, 0x00 }, { "xor", K_R, 0x33, 4, 0x00 },
    { "srl", K_R, 0x33, 5, 0x00 }, { "sra", K_R, 0x33, 5, 0x20 },
    { "or", K_R, 0x33, 6, 0x00 }, { "and", K_R, 0x33, 7, 0x00 },
    { "mul", K_R, 0x33, 0, 0x01 }, { "mulh", K_R, 0x33, 1, 0x01 },
    { "mulhsu", K_R, 0x33, 2, 0x01 }, { "mulhu", K_R, 0x33, 3, 0x01 },
    { "div", K_R, 0x33, 4, 0x01 }, { "divu", K_R, 0x33, 5, 0x01 },
    { "rem", K_R, 0x33, 6, 0x01 }, { "remu", K_R, 0x33, 7, 0x01 },
    { "addi", K_I, 0x13, 0, 0 }, { "slti", K_I, 0x13, 2, 0 },
    { "sltiu", K_I, 0x13, 3, 0 }, { "xori", K_I, 0x13, 4, 0 },
    { "ori", K_I, 0x13, 6, 0 }, { "andi", K_I, 0x13, 7, 0 },
    { "slli", K_SH, 0x13, 1, 0x00 }, { "srli", K_SH, 0x13, 5, 0x00 },
    { "srai", K_SH, 0x13, 5, 0x20 },
    { "lb", K_LD, 0x03, 0, 0 }, { "lh", K_LD, 0x03, 1, 0 },
    { "lw", K_LD, 0x03, 2, 0 }, { "lbu", K_LD, 0x03, 4, 0 },
    { "lhu", K_LD, 0x03, 5, 0 },
    { "sb", K_ST, 0x23, 0, 0 }, { "sh", K_ST, 0x23, 1, 0 },
    { "sw", K_ST, 0x23, 2, 0 },
    { "beq", K_BR, 0x63, 0, 0 }, { "bne", K_BR, 0x63, 1, 0 },
    { "blt", K_BR, 0x63, 4, 0 }, { "bge", K_BR, 0x63, 5, 0 },
    { "bltu", K_BR, 0x63, 6, 0 }, { "bgeu", K_BR, 0x63, 7, 0 },
    { "lui", K_LUI, 0x37, 0, 0 }, { "auipc", K_AUIPC, 0x17, 0, 0 },
    { "csrrw", K_CSR, 0x73, 1, 0 }, { "csrrs", K_CSR, 0x73, 2, 0 },
    { "csrrc", K_CSR, 0x73, 3, 0 }, { "csrrwi", K_CSRI, 0x73, 5, 0 },
    { "csrrsi", K_CSRI, 0x73, 6, 0 }, { "csrrci", K_CSRI, 0x73, 7, 0 },
    { "amoswap.w", K_AMO, 0x2f, 2, 0x01 }, { "amoadd.w", K_AMO, 0x2f, 2, 0x00 },
    { "amoxor.w", K_AMO, 0x2f, 2, 0x04 }, { "amoand.w", K_AMO, 0x2f, 2, 0x0c },
    { "amoor.w", K_AMO, 0x2f, 2, 0x08 }, { "amomin.w", K_AMO, 0x2f, 2, 0x10 },
    { "amomax.w", K_AMO, 0x2f, 2, 0x14 }, { "amominu.w", K_AMO, 0x2f, 2, 0x18 },
    { "amomaxu.w", K_AMO, 0x2f, 2, 0x1c }, { "sc.w", K_AMO, 0x2f, 2, 0x03 },
    { "lr.w", K_LR, 0x2f, 2, 0x02 },
    { 0, 0, 0, 0, 0 }
};

/* 被演算子を持たない命令 */
struct fixed {
    const char *name;
    unsigned long code;
};

static const struct fixed fixeds[] = {
    { "ecall", 0x00000073UL }, { "scall", 0x00000073UL },
    { "ebreak", 0x00100073UL }, { "sbreak", 0x00100073UL },
    { "sret", 0x10200073UL }, { "mret", 0x30200073UL },
    { "wfi", 0x10500073UL }, { "fence.i", 0x0000100fUL },
    { "nop", 0x00000013UL }, { "ret", 0x00008067UL },
    { "pause", 0x0100000fUL },
    { 0, 0 }
};

static const struct insn *findinsn(const char *name, int *aq, int *rl)
{
    int i;
    char base[32];
    long n = (long)strlen(name);
    *aq = *rl = 0;
    for (i = 0; insns[i].name; i++)
        if (strcmp(name, insns[i].name) == 0)
            return &insns[i];
    /* AMO の順序の接尾 */
    if (n < (long)sizeof(base)) {
        strcpy(base, name);
        if (n > 5 && strcmp(base + n - 5, ".aqrl") == 0) {
            base[n - 5] = 0;
            *aq = *rl = 1;
        } else if (n > 3 && strcmp(base + n - 3, ".aq") == 0) {
            base[n - 3] = 0;
            *aq = 1;
        } else if (n > 3 && strcmp(base + n - 3, ".rl") == 0) {
            base[n - 3] = 0;
            *rl = 1;
        } else
            return 0;
        for (i = 0; insns[i].name; i++)
            if (strcmp(base, insns[i].name) == 0
                && (insns[i].kind == K_AMO || insns[i].kind == K_LR))
                return &insns[i];
    }
    return 0;
}

/* fence の pred / succ */
static int fenceset(const char *s)
{
    int v = 0;
    if (isdig(*s))
        return (int)evalconst(s, 0) & 0xf;
    for (; *s; s++) {
        switch (*s) {
        case 'i': v |= 8; break;
        case 'o': v |= 4; break;
        case 'r': v |= 2; break;
        case 'w': v |= 1; break;
        default: fatal("fence の指定が読めない", s);
        }
    }
    return v;
}

static void needargs(int n, int want, const char *name)
{
    if (n != want)
        fatal("被演算子の数が違う", name);
}

/* 局所のラベルを今の位置に作る (la などの auipc の相手) */
static struct sym *pcrellabel(void)
{
    char buf[48];
    struct sym *s;
    sprintf(buf, ".Lpcrel_hi%ld", pcrelcnt++);
    s = lookup(buf, 1);
    defsym(s, cur, here());
    return s;
}

/* auipc rd, %pcrel_hi(sym) を出し，その位置のラベルを返す。続く命令は
 * そのラベルへの %pcrel_lo を持つ */
static struct sym *pcrelpair(int rd, const char *symexpr)
{
    struct sym *l = pcrellabel();
    long off = here();
    struct val v;
    evalstr(symexpr, &v);
    nosub(&v, symexpr);
    if (v.mod)
        fatal("修飾子は使えない", symexpr);
    if (pass == 2) {
        if (!v.s)
            fatal("番地が定数である", symexpr);
        addrel(off, R_RISCV_PCREL_HI20, v.s, v.c);
    }
    emit32(enc_u(0x17, rd, 0));
    return l;
}

static void lo_rel(struct sym *l, int stype)
{
    addrel(here(), stype ? R_RISCV_PCREL_LO12_S : R_RISCV_PCREL_LO12_I, l, 0);
}

/* li rd, imm (RV32)。1 命令で済まなければ lui + addi */
static void do_li(int rd, const char *s)
{
    int unk = 0;
    long long v = evalconst(s, &unk);
    int two;
    long idx = licnt++;
    if (v < -2147483648LL || v > 4294967295LL)
        fatal("li の値が 32 bit を超える", s);
    /* 0xfffff800 は -2048 として 1 命令で済む */
    v = ((v & 0xffffffffLL) ^ 0x80000000LL) - 0x80000000LL;
    if (pass == 1) {
        two = unk || v < -2048 || v > 2047;
        if (idx >= litwocap) {
            long nc = litwocap ? litwocap * 2 : 256;
            while (nc <= idx)
                nc *= 2;
            litwo = xrealloc(litwo, nc);
            litwocap = nc;
        }
        litwo[idx] = (unsigned char)two;
    } else {
        two = litwo[idx];
        if (unk)
            fatal("li の値が決まらない", s);
    }
    if (!two) {
        emit32(enc_i(0x13, 0, rd, 0, (long)v));
        return;
    }
    {
        long lo = (long)(((v & 0xfff) ^ 0x800) - 0x800);
        long hi = (long)(((v - lo) >> 12) & 0xfffff);
        emit32(enc_u(0x37, rd, hi));
        emit32(enc_i(0x13, 0, rd, rd, lo));
    }
}

/* 命令 1 つ (疑似命令を含む)。処理できれば 1 */
static int instruction(const char *name, char *args)
{
    char *a[8];
    int n = splitargs(args, a, 8);
    long off = here();
    const struct insn *in;
    int aq, rl, i;

    if (secs[cur].type == SHT_NOBITS)
        fatal("nobits の節に命令を置いた", name);
    /* 命令を置いた節は 4 バイトに揃える (圧縮命令を持たない) */
    if (secs[cur].align < 4)
        secs[cur].align = 4;

    for (i = 0; fixeds[i].name; i++)
        if (strcmp(name, fixeds[i].name) == 0 && n == 0) {
            emit32(fixeds[i].code);
            return 1;
        }

    in = findinsn(name, &aq, &rl);
    if (in) {
        switch (in->kind) {
        case K_R:
            needargs(n, 3, name);
            if (regno(a[2]) < 0 && in->f7 != 0x01 && !(in->f3 == 0 && in->f7 == 0x20)) {
                /* add a0, a0, 4 のような即値の形 (GNU as は I 形として受ける) */
                int rd = reg(a[0]), rs = reg(a[1]);
                if (in->f3 == 1 || in->f3 == 5) {
                    long long sh = evalconst(a[2], 0);
                    chkimm(sh, 0, 31, a[2]);
                    emit32(enc_r(0x13, in->f3, in->f7, rd, rs, (int)sh));
                } else
                    emit32(enc_i(0x13, in->f3, rd, rs, immI(a[2], 0, off)));
                return 1;
            }
            emit32(enc_r(in->op, in->f3, in->f7, reg(a[0]), reg(a[1]), reg(a[2])));
            return 1;
        case K_I:
            needargs(n, 3, name);
            {
                int rd = reg(a[0]), rs = reg(a[1]);
                long imm = immI(a[2], 0, off);
                emit32(enc_i(in->op, in->f3, rd, rs, imm));
            }
            return 1;
        case K_SH:
            needargs(n, 3, name);
            {
                int rd = reg(a[0]), rs = reg(a[1]);
                long long sh = evalconst(a[2], 0);
                chkimm(sh, 0, 31, a[2]);
                emit32(enc_r(in->op, in->f3, in->f7, rd, rs, (int)sh));
            }
            return 1;
        case K_LD:
            if (n == 2 && a[1][strlen(a[1]) - 1] != ')') {
                /* lw rd, sym: auipc rd + lw rd, %pcrel_lo(.L)(rd) */
                int rd = reg(a[0]);
                lo_rel(pcrelpair(rd, a[1]), 0);
                emit32(enc_i(in->op, in->f3, rd, rd, 0));
                return 1;
            }
            needargs(n, 2, name);
            {
                char *o;
                int base, rd = reg(a[0]);
                long imm;
                memop(a[1], &o, &base);
                imm = *o ? immI(o, 0, off) : 0;
                emit32(enc_i(in->op, in->f3, rd, base, imm));
            }
            return 1;
        case K_ST:
            if (n == 3) {
                /* sw rs, sym, rt */
                int rs = reg(a[0]), rt = reg(a[2]);
                lo_rel(pcrelpair(rt, a[1]), 1);
                emit32(enc_s(in->op, in->f3, rt, rs, 0));
                return 1;
            }
            needargs(n, 2, name);
            {
                char *o;
                int base, rs = reg(a[0]);
                long imm;
                memop(a[1], &o, &base);
                imm = *o ? immI(o, 1, off) : 0;
                emit32(enc_s(in->op, in->f3, base, rs, imm));
            }
            return 1;
        case K_BR:
            needargs(n, 3, name);
            {
                int r1 = reg(a[0]), r2 = reg(a[1]);
                long d = target(a[2], off, R_RISCV_BRANCH);
                emit32(enc_b(in->op, in->f3, r1, r2, d));
            }
            return 1;
        case K_LUI:
        case K_AUIPC:
            needargs(n, 2, name);
            {
                int rd = reg(a[0]);
                long imm = immU(a[1], off, in->kind == K_AUIPC);
                emit32(enc_u(in->op, rd, imm));
            }
            return 1;
        case K_CSR:
            needargs(n, 3, name);
            {
                int rd = reg(a[0]), c = csrno(a[1]);
                if (regno(a[2]) < 0) {
                    /* 即値なら csrr?i の形 (GNU as と同じ) */
                    long long u = evalconst(a[2], 0);
                    chkimm(u, 0, 31, a[2]);
                    emit32(enc_i(in->op, in->f3 + 4, rd, (int)u, c));
                } else
                    emit32(enc_i(in->op, in->f3, rd, reg(a[2]), c));
            }
            return 1;
        case K_CSRI:
            needargs(n, 3, name);
            {
                int rd = reg(a[0]), c = csrno(a[1]);
                long long u = evalconst(a[2], 0);
                chkimm(u, 0, 31, a[2]);
                emit32(enc_i(in->op, in->f3, rd, (int)u, c));
            }
            return 1;
        case K_AMO:
        case K_LR:
            needargs(n, in->kind == K_LR ? 2 : 3, name);
            {
                char *o;
                int base, rd = reg(a[0]), rs2 = 0;
                if (in->kind == K_AMO)
                    rs2 = reg(a[1]);
                memop(a[n - 1], &o, &base);
                if (*o && evalconst(o, 0) != 0)
                    fatal("AMO の番地に 0 以外の変位", o);
                emit32(enc_r(in->op, in->f3, (in->f7 << 2) | (aq << 1) | rl,
                             rd, base, rs2));
            }
            return 1;
        }
    }

    /* 疑似命令 */
    if (strcmp(name, "li") == 0) {
        needargs(n, 2, name);
        do_li(reg(a[0]), a[1]);
        return 1;
    }
    if (strcmp(name, "la") == 0 || strcmp(name, "lla") == 0) {
        int rd;
        needargs(n, 2, name);
        rd = reg(a[0]);
        lo_rel(pcrelpair(rd, a[1]), 0);
        emit32(enc_i(0x13, 0, rd, rd, 0));
        return 1;
    }
    if (strcmp(name, "mv") == 0 || strcmp(name, "move") == 0) {
        needargs(n, 2, name);
        emit32(enc_i(0x13, 0, reg(a[0]), reg(a[1]), 0));
        return 1;
    }
    if (strcmp(name, "not") == 0) {
        needargs(n, 2, name);
        emit32(enc_i(0x13, 4, reg(a[0]), reg(a[1]), -1));
        return 1;
    }
    if (strcmp(name, "neg") == 0) {
        needargs(n, 2, name);
        emit32(enc_r(0x33, 0, 0x20, reg(a[0]), 0, reg(a[1])));
        return 1;
    }
    if (strcmp(name, "seqz") == 0) {
        needargs(n, 2, name);
        emit32(enc_i(0x13, 3, reg(a[0]), reg(a[1]), 1));
        return 1;
    }
    if (strcmp(name, "snez") == 0) {
        needargs(n, 2, name);
        emit32(enc_r(0x33, 3, 0, reg(a[0]), 0, reg(a[1])));
        return 1;
    }
    if (strcmp(name, "sltz") == 0) {
        needargs(n, 2, name);
        emit32(enc_r(0x33, 2, 0, reg(a[0]), reg(a[1]), 0));
        return 1;
    }
    if (strcmp(name, "sgtz") == 0) {
        needargs(n, 2, name);
        emit32(enc_r(0x33, 2, 0, reg(a[0]), 0, reg(a[1])));
        return 1;
    }
    if (strcmp(name, "sgt") == 0 || strcmp(name, "sgtu") == 0) {
        needargs(n, 3, name);
        emit32(enc_r(0x33, name[3] == 'u' ? 3 : 2, 0, reg(a[0]), reg(a[2]), reg(a[1])));
        return 1;
    }
    if (strcmp(name, "zext.b") == 0) {
        needargs(n, 2, name);
        emit32(enc_i(0x13, 7, reg(a[0]), reg(a[1]), 255));
        return 1;
    }
    {
        /* 0 と比べる分岐 */
        static const char *zb[] = { "beqz", "bnez", "blez", "bgez", "bltz", "bgtz", 0 };
        static const int zf3[] = { 0, 1, 5, 5, 4, 4 };
        static const int zswap[] = { 0, 0, 1, 0, 0, 1 };
        for (i = 0; zb[i]; i++)
            if (strcmp(name, zb[i]) == 0) {
                int r;
                long d;
                needargs(n, 2, name);
                r = reg(a[0]);
                d = target(a[1], off, R_RISCV_BRANCH);
                if (zswap[i])
                    emit32(enc_b(0x63, zf3[i], 0, r, d));
                else
                    emit32(enc_b(0x63, zf3[i], r, 0, d));
                return 1;
            }
    }
    {
        /* 被演算子を入れ替える分岐 */
        static const char *sb[] = { "bgt", "ble", "bgtu", "bleu", 0 };
        static const int sf3[] = { 4, 5, 6, 7 };
        for (i = 0; sb[i]; i++)
            if (strcmp(name, sb[i]) == 0) {
                int r1, r2;
                long d;
                needargs(n, 3, name);
                r1 = reg(a[0]);
                r2 = reg(a[1]);
                d = target(a[2], off, R_RISCV_BRANCH);
                emit32(enc_b(0x63, sf3[i], r2, r1, d));
                return 1;
            }
    }
    if (strcmp(name, "j") == 0) {
        needargs(n, 1, name);
        emit32(enc_j(0x6f, 0, target(a[0], off, R_RISCV_JAL)));
        return 1;
    }
    if (strcmp(name, "jal") == 0) {
        if (n == 1) {
            emit32(enc_j(0x6f, 1, target(a[0], off, R_RISCV_JAL)));
            return 1;
        }
        needargs(n, 2, name);
        {
            int rd = reg(a[0]);
            emit32(enc_j(0x6f, rd, target(a[1], off, R_RISCV_JAL)));
        }
        return 1;
    }
    if (strcmp(name, "jr") == 0 || strcmp(name, "jalr") == 0) {
        int link = name[1] == 'a' ? 1 : 0;
        int rd = link, rs;
        long imm = 0;
        char *o;
        if (n == 1) {
            if (regno(a[0]) >= 0)
                rs = reg(a[0]);
            else {
                memop(a[0], &o, &rs);
                imm = *o ? immI(o, 0, off) : 0;
            }
        } else if (n == 2 && link) {
            rd = reg(a[0]);
            if (regno(a[1]) >= 0)
                rs = reg(a[1]);
            else {
                memop(a[1], &o, &rs);
                imm = *o ? immI(o, 0, off) : 0;
            }
        } else if (n == 3 && link) {
            rd = reg(a[0]);
            rs = reg(a[1]);
            imm = immI(a[2], 0, off);
        } else {
            fatal("被演算子の数が違う", name);
            return 1;
        }
        emit32(enc_i(0x67, 0, rd, rs, imm));
        return 1;
    }
    if (strcmp(name, "call") == 0 || strcmp(name, "tail") == 0) {
        int tail = name[0] == 't';
        int rt = tail ? 6 : 1, rd;
        const char *t;
        struct val v;
        if (n == 2) {
            rt = reg(a[0]);
            t = a[1];
        } else {
            needargs(n, 1, name);
            t = a[0];
        }
        rd = tail ? 0 : rt;
        evalstr(t, &v);
        nosub(&v, t);
        if (v.mod)
            fatal("修飾子は使えない", t);
        if (pass == 2) {
            if (!v.s)
                fatal("呼ぶ先が定数である", t);
            addrel(off, R_RISCV_CALL_PLT, v.s, v.c);
        }
        emit32(enc_u(0x17, rt, 0));
        emit32(enc_i(0x67, 0, rd, rt, 0));
        return 1;
    }
    if (strcmp(name, "fence") == 0) {
        int pred = 15, succ = 15;
        if (n == 2) {
            pred = fenceset(a[0]);
            succ = fenceset(a[1]);
        } else if (n != 0)
            fatal("被演算子の数が違う", name);
        emit32(((unsigned long)pred << 24) | ((unsigned long)succ << 20) | 0x0f);
        return 1;
    }
    if (strcmp(name, "sfence.vma") == 0) {
        int r1 = 0, r2 = 0;
        if (n >= 1)
            r1 = reg(a[0]);
        if (n >= 2)
            r2 = reg(a[1]);
        if (n > 2)
            fatal("被演算子の数が違う", name);
        emit32(enc_r(0x73, 0, 0x09, 0, r1, r2));
        return 1;
    }
    {
        /* CSR の疑似命令 */
        static const char *cr[] = { "rdcycle", "rdtime", "rdinstret",
                                    "rdcycleh", "rdtimeh", "rdinstreth", 0 };
        static const int cn[] = { 0xc00, 0xc01, 0xc02, 0xc80, 0xc81, 0xc82 };
        for (i = 0; cr[i]; i++)
            if (strcmp(name, cr[i]) == 0) {
                needargs(n, 1, name);
                emit32(enc_i(0x73, 2, reg(a[0]), 0, cn[i]));
                return 1;
            }
    }
    if (strcmp(name, "csrr") == 0) {
        needargs(n, 2, name);
        {
            int rd = reg(a[0]);
            emit32(enc_i(0x73, 2, rd, 0, csrno(a[1])));
        }
        return 1;
    }
    if (strcmp(name, "csrw") == 0 || strcmp(name, "csrs") == 0 || strcmp(name, "csrc") == 0) {
        int f3 = name[3] == 'w' ? 1 : name[3] == 's' ? 2 : 3;
        needargs(n, 2, name);
        {
            int c = csrno(a[0]);
            if (regno(a[1]) < 0) {
                long long u = evalconst(a[1], 0);
                chkimm(u, 0, 31, a[1]);
                emit32(enc_i(0x73, f3 + 4, 0, (int)u, c));
            } else
                emit32(enc_i(0x73, f3, 0, reg(a[1]), c));
        }
        return 1;
    }
    if (strcmp(name, "csrwi") == 0 || strcmp(name, "csrsi") == 0 || strcmp(name, "csrci") == 0) {
        int f3 = name[3] == 'w' ? 5 : name[3] == 's' ? 6 : 7;
        needargs(n, 2, name);
        {
            int c = csrno(a[0]);
            long long u = evalconst(a[1], 0);
            chkimm(u, 0, 31, a[1]);
            emit32(enc_i(0x73, f3, 0, (int)u, c));
        }
        return 1;
    }
    return 0;
}

/* ---- 擬似命令 (directive) ---- */
static char *unquote(const char *s, long *len)
{
    char *buf;
    long n = 0;
    s = skipsp(s);
    if (*s != '"')
        fatal("文字列でない", s);
    buf = xmalloc((long)strlen(s) + 1);
    ep = s + 1;
    while (*ep && *ep != '"')
        buf[n++] = (char)parseesc();
    if (*ep != '"')
        fatal("文字列が閉じていない", s);
    ep++;
    buf[n] = 0;
    *len = n;
    return buf;
}

static void data(char *args, int size)
{
    char *a[256];
    int n = splitargs(args, a, 256), i;
    for (i = 0; i < n; i++) {
        struct val v;
        long off = here();
        evalstr(a[i], &v);
        if (v.mod)
            fatal("データに修飾子は使えない", a[i]);
        if (v.sub && pass == 2) {
            static const int addr[9] = { 0, R_RISCV_ADD8, R_RISCV_ADD16, 0, R_RISCV_ADD32,
                                         0, 0, 0, R_RISCV_ADD64 };
            static const int subr[9] = { 0, R_RISCV_SUB8, R_RISCV_SUB16, 0, R_RISCV_SUB32,
                                         0, 0, 0, R_RISCV_SUB64 };
            if (!v.s)
                fatal("定数から記号を引いた", a[i]);
            addrel(off, addr[size], v.s, v.c);
            addrel(off, subr[size], v.sub, 0);
            emitn(0, size);
        } else if (v.s && pass == 2) {
            if (size == 4)
                addrel(off, R_RISCV_32, v.s, v.c);
            else if (size == 8)
                addrel(off, R_RISCV_64, v.s, v.c);
            else
                fatal("記号を置けるのは 4 / 8 バイトだけ", a[i]);
            emitn(0, size);
        } else
            emitn((unsigned long long)v.c, size);
    }
}

static void align_to(long a, int fill, int hasfill)
{
    struct sec *s = &secs[cur];
    long pad;
    if (a <= 0 || (a & (a - 1)))
        fatal("整列が 2 の冪でない", 0);
    if (a > s->align)
        s->align = a;
    pad = (a - (s->size % a)) % a;
    /* 命令の節は nop で埋める (4 の倍数の部分) */
    if (!hasfill && (s->flags & SHF_EXECINSTR) && s->type != SHT_NOBITS) {
        while (pad % 4) {
            emitbyte(0);
            pad--;
        }
        while (pad > 0) {
            emit32(0x00000013UL);
            pad -= 4;
        }
        return;
    }
    while (pad-- > 0)
        emitbyte(fill);
}

static int typeval(const char *t)
{
    t = skipsp(t);
    if (*t == '@' || *t == '%' || *t == '#')
        t++;
    if (strcmp(t, "function") == 0 || strcmp(t, "STT_FUNC") == 0)
        return STT_FUNC;
    if (strcmp(t, "object") == 0 || strcmp(t, "STT_OBJECT") == 0)
        return STT_OBJECT;
    if (strcmp(t, "notype") == 0 || strcmp(t, "STT_NOTYPE") == 0)
        return STT_NOTYPE;
    fatal("知らない記号の型", t);
    return 0;
}

/* .section の名前 (引用符付きでもよい) */
static char *secname(char *s)
{
    long n;
    s = (char *)skipsp(s);
    if (*s == '"')
        return unquote(s, &n);
    return xstrdup(s);
}

static void do_section(char *args, int push)
{
    char *a[8];
    int n = splitargs(args, a, 8);
    char *name;
    int type = SHT_PROGBITS, has = 0, i;
    unsigned long flags = 0;
    long entsize = 0;
    if (n < 1)
        fatal(".section に名前が無い", 0);
    name = secname(a[0]);
    if (n >= 2) {
        long len;
        char *f = unquote(a[1], &len);
        const char *p;
        has = 1;
        for (p = f; *p; p++) {
            switch (*p) {
            case 'a': flags |= SHF_ALLOC; break;
            case 'w': flags |= SHF_WRITE; break;
            case 'x': flags |= SHF_EXECINSTR; break;
            case 'M': flags |= SHF_MERGE; break;
            case 'S': flags |= SHF_STRINGS; break;
            case 'T': flags |= SHF_TLS; break;
            default: fatal("知らない節の旗", f);
            }
        }
        free(f);
        {
            int dt;
            unsigned long df;
            secdefaults(name, &dt, &df);
            type = dt;
        }
    }
    if (n >= 3) {
        const char *t = skipsp(a[2]);
        if (*t == '@' || *t == '%')
            t++;
        if (strcmp(t, "progbits") == 0)
            type = SHT_PROGBITS;
        else if (strcmp(t, "nobits") == 0)
            type = SHT_NOBITS;
        else if (strcmp(t, "note") == 0)
            type = SHT_NOTE;
        else if (strcmp(t, "init_array") == 0)
            type = SHT_INIT_ARRAY;
        else if (strcmp(t, "fini_array") == 0)
            type = SHT_FINI_ARRAY;
        else
            fatal("知らない節の型", t);
    }
    if (n >= 4 && (flags & SHF_MERGE))
        entsize = (long)evalconst(a[3], 0);
    if (n >= 4 && !(flags & SHF_MERGE))
        fatal(".section の引数が多い", a[3]);
    i = getsec(name, has, type, flags, entsize);
    free(name);
    if (push) {
        if (nsecstack >= MAXSTACK)
            fatal(".pushsection が深すぎる", 0);
        secstack[nsecstack] = cur;
        prevstack[nsecstack] = prevsec;
        nsecstack++;
    }
    setsec(i);
}

static void incbin(char *args)
{
    char *a[4];
    int n = splitargs(args, a, 4), i;
    long len, skip = 0, count = -1;
    char *fn = unquote(a[0], &len);
    FILE *f = 0;
    int c;
    if (n >= 2)
        skip = (long)evalconst(a[1], 0);
    if (n >= 3)
        count = (long)evalconst(a[2], 0);
    f = fopen(fn, "rb");
    for (i = 0; !f && i < nincdir; i++) {
        char *p = xmalloc((long)strlen(incdirs[i]) + len + 2);
        sprintf(p, "%s/%s", incdirs[i], fn);
        f = fopen(p, "rb");
        free(p);
    }
    if (!f)
        fatal(".incbin のファイルが開けない", fn);
    free(fn);
    while (skip > 0 && getc(f) != EOF)
        skip--;
    while (count != 0 && (c = getc(f)) != EOF) {
        emitbyte(c);
        if (count > 0)
            count--;
    }
    fclose(f);
}

static void process(char *stmt);

static struct macro *findmacro(const char *name)
{
    struct macro *m;
    for (m = macros; m; m = m->next)
        if (strcmp(m->name, name) == 0)
            return m;
    return 0;
}

/* 定義中のマクロ (.macro から .endm まで) */
static struct macro *defining;
static int defdepth;

static void begin_macro(char *args)
{
    struct macro *m = xmalloc(sizeof(struct macro));
    char *p = (char *)skipsp(args), *q;
    memset(m, 0, sizeof(struct macro));
    q = p;
    while (*q && !issp(*q) && *q != ',')
        q++;
    m->name = xstrndup(p, q - p);
    p = q;
    for (;;) {
        char *e;
        p = (char *)skipsp(p);
        if (*p == ',')
            p = (char *)skipsp(p + 1);
        if (!*p)
            break;
        e = p;
        while (*e && !issp(*e) && *e != ',' && *e != '=')
            e++;
        if (m->nparam >= MAXMACARG)
            fatal("マクロの引数が多すぎる", m->name);
        m->param[m->nparam] = xstrndup(p, e - p);
        p = e;
        if (*p == '=') {
            char *d = ++p;
            while (*p && !issp(*p) && *p != ',')
                p++;
            m->defval[m->nparam] = xstrndup(d, p - d);
        }
        {
            /* 接尾の :req / :vararg は読み捨てる */
            char *c = strchr(m->param[m->nparam], ':');
            if (c)
                *c = 0;
        }
        m->nparam++;
    }
    defining = m;
    defdepth = 0;
}

static void end_macro(void)
{
    struct macro *m = defining, *o;
    defining = 0;
    o = findmacro(m->name);
    if (o) {
        /* 2 パス目の再定義は同じ物なので上書きする */
        o->nparam = m->nparam;
        memcpy(o->param, m->param, sizeof(m->param));
        memcpy(o->defval, m->defval, sizeof(m->defval));
        o->nline = m->nline;
        o->line = m->line;
        return;
    }
    m->next = macros;
    macros = m;
}

static void macro_line(const char *s)
{
    struct macro *m = defining;
    m->line = xrealloc(m->line, (long)(m->nline + 1) * (long)sizeof(char *));
    m->line[m->nline++] = xstrdup(s);
}

/* マクロ呼出しの引数を分ける。, が無ければ空白で区切る */
static int macroargs(char *s, char **out, int max)
{
    int n = 0;
    char *p = (char *)skipsp(s);
    int hascomma = 0, depth = 0, instr = 0;
    char *q;
    for (q = p; *q; q++) {
        if (instr) {
            if (*q == '"')
                instr = 0;
            continue;
        }
        if (*q == '"')
            instr = 1;
        else if (*q == '(')
            depth++;
        else if (*q == ')')
            depth--;
        else if (*q == ',' && depth == 0)
            hascomma = 1;
    }
    if (hascomma)
        return splitargs(p, out, max);
    while (*p) {
        char *e = p;
        depth = 0;
        while (*e && (depth > 0 || !issp(*e))) {
            if (*e == '(')
                depth++;
            else if (*e == ')')
                depth--;
            e++;
        }
        if (n >= max)
            fatal("マクロの引数が多すぎる", 0);
        out[n++] = p;
        if (*e)
            *e++ = 0;
        p = (char *)skipsp(e);
    }
    return n;
}

/* 置換: \param，\()，LOCAL で決めた名前 */
static char *substitute(const char *line, struct macro *m, char **argv,
                        char **lname, char **lrepl, int nlocal)
{
    long cap = (long)strlen(line) * 2 + 256, n = 0;
    char *out = xmalloc(cap);
    const char *p = line;
    while (*p) {
        const char *rep = 0;
        long skip = 0;
        if (*p == '\\') {
            if (p[1] == '(' && p[2] == ')') {
                p += 3;
                continue;
            } else {
                int i;
                const char *q = p + 1;
                while (isid(*q))
                    q++;
                for (i = 0; i < m->nparam; i++)
                    if ((long)strlen(m->param[i]) == q - p - 1
                        && strncmp(m->param[i], p + 1, q - p - 1) == 0) {
                        rep = argv[i];
                        skip = q - p;
                        break;
                    }
            }
        } else if (nlocal && isid0(*p) && (p == line || !isid(p[-1]))) {
            int i;
            const char *q = p;
            while (isid(*q))
                q++;
            for (i = 0; i < nlocal; i++)
                if ((long)strlen(lname[i]) == q - p && strncmp(lname[i], p, q - p) == 0) {
                    rep = lrepl[i];
                    skip = q - p;
                    break;
                }
        }
        if (rep) {
            long rl = (long)strlen(rep);
            while (n + rl + 2 > cap) {
                cap *= 2;
                out = xrealloc(out, cap);
            }
            memcpy(out + n, rep, rl);
            n += rl;
            p += skip;
            continue;
        }
        if (n + 2 > cap) {
            cap *= 2;
            out = xrealloc(out, cap);
        }
        out[n++] = *p++;
    }
    out[n] = 0;
    return out;
}

static void expand_macro(struct macro *m, char *args)
{
    char *argv[MAXMACARG];
    char *given[MAXMACARG];
    char *lname[32], *lrepl[32];
    int ng, i, nlocal = 0;
    long savedline = lineno;
    if (++macdepth > 64)
        fatal("マクロの展開が深すぎる", m->name);
    ng = macroargs(args, given, MAXMACARG);
    if (ng > m->nparam)
        fatal("マクロの引数が多すぎる", m->name);
    for (i = 0; i < m->nparam; i++) {
        if (i < ng && *given[i])
            argv[i] = given[i];
        else if (m->defval[i])
            argv[i] = m->defval[i];
        else
            argv[i] = "";
    }
    /* LOCAL (altmacro) を先に集める */
    for (i = 0; i < m->nline; i++) {
        const char *p = skipsp(m->line[i]);
        if (altmacro && strncmp(p, "LOCAL", 5) == 0 && issp(p[5])) {
            char *copy = xstrdup(p + 6), *a[32];
            int k, na = splitargs(copy, a, 32);
            for (k = 0; k < na; k++) {
                char buf[48];
                if (nlocal >= 32)
                    fatal("LOCAL が多すぎる", m->name);
                lname[nlocal] = xstrdup(a[k]);
                sprintf(buf, ".LL%04lx", localcnt++);
                lrepl[nlocal] = xstrdup(buf);
                nlocal++;
            }
            free(copy);
        }
    }
    for (i = 0; i < m->nline; i++) {
        const char *p = skipsp(m->line[i]);
        char *s;
        if (altmacro && strncmp(p, "LOCAL", 5) == 0 && issp(p[5]))
            continue;
        s = substitute(m->line[i], m, argv, lname, lrepl, nlocal);
        lineno = savedline;
        process(s);
        free(s);
    }
    for (i = 0; i < nlocal; i++) {
        free(lname[i]);
        free(lrepl[i]);
    }
    macdepth--;
}

static int active(void)
{
    int i;
    for (i = 0; i < nif; i++)
        if (!ifstack[i])
            return 0;
    return 1;
}

static void pushif(int cond)
{
    if (nif >= MAXSTACK)
        fatal(".if が深すぎる", 0);
    ifstack[nif] = cond;
    ifdone[nif] = cond;
    nif++;
}

static void setsize(char *args)
{
    char *a[2];
    struct sym *s;
    int unk = 0;
    long long v;
    needargs(splitargs(args, a, 2), 2, ".size");
    s = lookup(a[0], 1);
    v = evalconst(a[1], &unk);
    if (pass == 2) {
        if (unk)
            fatal(".size の値が決まらない", a[1]);
        s->size = v;
        s->has_size = 1;
    }
}

static void setsym(const char *name, const char *e)
{
    struct sym *s = lookup(name, 1);
    struct val v;
    evalstr(e, &v);
    nosub(&v, e);
    if (v.mod)
        fatal("修飾子は使えない", e);
    if (v.unk) {
        if (pass == 2)
            fatal("値が決まらない", e);
        s->sec = S_ABS;
        s->val = 0;
        s->defpass = pass;
        return;
    }
    if (!v.s) {
        s->sec = S_ABS;
        s->val = v.c;
        s->alias = 0;
    } else if (v.s->type == STT_SECTION) {
        s->sec = v.s->sec;
        s->val = v.c;
        s->alias = 0;
    } else {
        s->sec = S_ALIAS;
        s->alias = v.s;
        s->val = v.c;
    }
    s->defpass = pass;
}

static void bindsyms(char *args, int bind)
{
    char *a[64];
    int n = splitargs(args, a, 64), i;
    for (i = 0; i < n; i++) {
        struct sym *s = lookup(a[i], 1);
        s->bind = bind;
    }
}

/* 擬似命令 1 つ。処理できれば 1 */
static int directive(const char *d, char *args)
{
    if (strcmp(d, ".text") == 0) { setsec(getsec(".text", 0, 0, 0, 0)); return 1; }
    if (strcmp(d, ".data") == 0) { setsec(getsec(".data", 0, 0, 0, 0)); return 1; }
    if (strcmp(d, ".bss") == 0) { setsec(getsec(".bss", 0, 0, 0, 0)); return 1; }
    if (strcmp(d, ".rodata") == 0) { setsec(getsec(".rodata", 0, 0, 0, 0)); return 1; }
    if (strcmp(d, ".section") == 0) { do_section(args, 0); return 1; }
    if (strcmp(d, ".pushsection") == 0) { do_section(args, 1); return 1; }
    if (strcmp(d, ".popsection") == 0) {
        if (nsecstack == 0)
            fatal(".popsection に対応する .pushsection が無い", 0);
        nsecstack--;
        cur = secstack[nsecstack];
        prevsec = prevstack[nsecstack];
        return 1;
    }
    if (strcmp(d, ".previous") == 0) {
        int t = cur;
        cur = prevsec;
        prevsec = t;
        return 1;
    }
    if (strcmp(d, ".byte") == 0) { data(args, 1); return 1; }
    if (strcmp(d, ".2byte") == 0 || strcmp(d, ".half") == 0 || strcmp(d, ".short") == 0) {
        data(args, 2);
        return 1;
    }
    if (strcmp(d, ".4byte") == 0 || strcmp(d, ".word") == 0 || strcmp(d, ".long") == 0
        || strcmp(d, ".int") == 0) {
        data(args, 4);
        return 1;
    }
    if (strcmp(d, ".8byte") == 0 || strcmp(d, ".dword") == 0 || strcmp(d, ".quad") == 0) {
        data(args, 8);
        return 1;
    }
    if (strcmp(d, ".ascii") == 0 || strcmp(d, ".asciz") == 0 || strcmp(d, ".string") == 0) {
        char *a[64];
        int n = splitargs(args, a, 64), i;
        for (i = 0; i < n; i++) {
            long len, k;
            char *s = unquote(a[i], &len);
            for (k = 0; k < len; k++)
                emitbyte((unsigned char)s[k]);
            /* .asciz と .string は終端の 0 を置く。.ascii は置かない */
            if (strcmp(d, ".ascii") != 0)
                emitbyte(0);
            free(s);
        }
        return 1;
    }
    if (strcmp(d, ".zero") == 0 || strcmp(d, ".skip") == 0 || strcmp(d, ".space") == 0) {
        char *a[2];
        int n = splitargs(args, a, 2);
        long long cnt = evalconst(a[0], 0), fill = n > 1 ? evalconst(a[1], 0) : 0;
        if (cnt < 0)
            fatal("負の大きさ", a[0]);
        while (cnt-- > 0)
            emitbyte((int)(fill & 0xff));
        return 1;
    }
    if (strcmp(d, ".fill") == 0) {
        char *a[3];
        int n = splitargs(args, a, 3);
        long long rep = evalconst(a[0], 0);
        long long size = n > 1 ? evalconst(a[1], 0) : 1;
        long long v = n > 2 ? evalconst(a[2], 0) : 0;
        if (size > 8)
            size = 8;
        while (rep-- > 0) {
            emitn((unsigned long long)v & 0xffffffffULL, (int)(size > 4 ? 4 : size));
            if (size > 4)
                emitn(0, (int)(size - 4));
        }
        return 1;
    }
    if (strcmp(d, ".balign") == 0 || strcmp(d, ".p2align") == 0 || strcmp(d, ".align") == 0) {
        char *a[3];
        int n = splitargs(args, a, 3);
        long long v = evalconst(a[0], 0);
        int fill = 0, hasfill = 0;
        if (n >= 2 && *a[1]) {
            fill = (int)evalconst(a[1], 0);
            hasfill = 1;
        }
        if (d[1] != 'b') {
            if (v < 0 || v > 30)
                fatal("整列が範囲外", a[0]);
            v = 1L << v;
        }
        align_to((long)v, fill, hasfill);
        return 1;
    }
    if (strcmp(d, ".globl") == 0 || strcmp(d, ".global") == 0) { bindsyms(args, STB_GLOBAL); return 1; }
    if (strcmp(d, ".weak") == 0) { bindsyms(args, STB_WEAK); return 1; }
    if (strcmp(d, ".local") == 0) { bindsyms(args, STB_LOCAL); return 1; }
    if (strcmp(d, ".hidden") == 0) {
        char *a[64];
        int n = splitargs(args, a, 64), i;
        for (i = 0; i < n; i++)
            lookup(a[i], 1)->hidden = 1;
        return 1;
    }
    if (strcmp(d, ".type") == 0) {
        char *a[2];
        int n = splitargs(args, a, 2);
        if (n == 1) {
            /* .type sym STT_FUNC (カンマを省いた形) */
            char *sp = a[0];
            while (*sp && !issp(*sp))
                sp++;
            if (*sp) {
                *sp = 0;
                a[1] = (char *)skipsp(sp + 1);
                n = 2;
            }
        }
        needargs(n, 2, d);
        lookup(a[0], 1)->type = typeval(a[1]);
        return 1;
    }
    if (strcmp(d, ".size") == 0) { setsize(args); return 1; }
    if (strcmp(d, ".set") == 0 || strcmp(d, ".equ") == 0 || strcmp(d, ".equiv") == 0) {
        char *a[2];
        needargs(splitargs(args, a, 2), 2, d);
        setsym(a[0], a[1]);
        return 1;
    }
    if (strcmp(d, ".comm") == 0 || strcmp(d, ".common") == 0) {
        char *a[3];
        int n = splitargs(args, a, 3);
        struct sym *s = lookup(a[0], 1);
        s->sec = S_COMMON;
        s->type = STT_OBJECT;
        s->size = evalconst(a[1], 0);
        s->has_size = 1;
        s->val = n > 2 ? evalconst(a[2], 0) : 4;
        s->defpass = pass;
        if (s->bind == STB_LOCAL)
            s->bind = STB_GLOBAL;
        return 1;
    }
    if (strcmp(d, ".incbin") == 0) { incbin(args); return 1; }
    if (strcmp(d, ".macro") == 0) { begin_macro(args); return 1; }
    if (strcmp(d, ".altmacro") == 0) { altmacro = 1; return 1; }
    if (strcmp(d, ".noaltmacro") == 0) { altmacro = 0; return 1; }
    if (strcmp(d, ".purgem") == 0) return 1;
    if (strcmp(d, ".err") == 0) { fatal(".err", 0); return 1; }
    if (strcmp(d, ".error") == 0) { fatal(".error", args); return 1; }
    if (strcmp(d, ".option") == 0 || strcmp(d, ".file") == 0 || strcmp(d, ".ident") == 0
        || strcmp(d, ".attribute") == 0 || strcmp(d, ".loc") == 0
        || strcmp(d, ".addrsig") == 0 || strcmp(d, ".end") == 0
        || strncmp(d, ".cfi_", 5) == 0)
        return 1;
    return 0;
}

/* 文を 1 つ処理する (ラベル・擬似命令・命令・マクロ) */
static void process(char *stmt)
{
    char *p = (char *)skipsp(stmt);
    char *name, *q;

    if (defining) {
        const char *t = p;
        if (strncmp(t, ".endm", 5) == 0 && !isid(t[5])) {
            if (defdepth == 0) {
                end_macro();
                return;
            }
            defdepth--;
        } else if (strncmp(t, ".macro", 6) == 0 && issp(t[6]))
            defdepth++;
        macro_line(stmt);
        return;
    }

    /* 条件の擬似命令は無効な区間でも数える */
    if (strncmp(p, ".if", 3) == 0 || strncmp(p, ".else", 5) == 0
        || strncmp(p, ".endif", 6) == 0) {
        q = p;
        while (isid(*q))
            q++;
        name = xstrndup(p, q - p);
        if (strcmp(name, ".endif") == 0) {
            if (nif == 0)
                fatal(".endif に対応する .if が無い", 0);
            nif--;
            free(name);
            return;
        }
        if (strcmp(name, ".else") == 0) {
            if (nif == 0)
                fatal(".else に対応する .if が無い", 0);
            ifstack[nif - 1] = !ifdone[nif - 1];
            ifdone[nif - 1] = 1;
            free(name);
            return;
        }
        if (!active()) {
            pushif(0);
            ifdone[nif - 1] = 1;
            free(name);
            return;
        }
        if (strcmp(name, ".if") == 0)
            pushif(evalconst(q, 0) != 0);
        else if (strcmp(name, ".ifdef") == 0) {
            struct sym *s = lookup(skipsp(q), 0);
            pushif(s && s->defpass > 0);
        } else if (strcmp(name, ".ifndef") == 0 || strcmp(name, ".ifnotdef") == 0) {
            struct sym *s = lookup(skipsp(q), 0);
            pushif(!(s && s->defpass > 0));
        } else if (strcmp(name, ".ifeq") == 0)
            pushif(evalconst(q, 0) == 0);
        else if (strcmp(name, ".ifne") == 0)
            pushif(evalconst(q, 0) != 0);
        else
            fatal("知らない条件", name);
        free(name);
        return;
    }
    if (!active())
        return;

    /* ラベル */
    for (;;) {
        q = p;
        if (isdig(*q)) {
            while (isdig(*q))
                q++;
            if (*q == ':') {
                long n = strtol(p, 0, 10);
                struct numlab *nl = numlab(n);
                defsym(numsym(n, nl->cnt), cur, here());
                nl->cnt++;
                p = (char *)skipsp(q + 1);
                continue;
            }
            break;
        }
        if (!isid0(*q))
            break;
        while (isid(*q))
            q++;
        if (*q == ':') {
            char *ln = xstrndup(p, q - p);
            struct sym *s = lookup(ln, 1);
            free(ln);
            defsym(s, cur, here());
            p = (char *)skipsp(q + 1);
            continue;
        }
        break;
    }
    if (!*p)
        return;

    q = p;
    while (*q && !issp(*q))
        q++;
    name = xstrndup(p, q - p);
    q = (char *)skipsp(q);

    /* sym = expr */
    if (*q == '=' && q[1] != '=') {
        setsym(name, q + 1);
        free(name);
        return;
    }
    if (name[0] == '.' && directive(name, q)) {
        free(name);
        return;
    }
    {
        struct macro *m = findmacro(name);
        if (m) {
            expand_macro(m, q);
            free(name);
            return;
        }
    }
    {
        /* 大文字の命令名も受ける */
        char *lc = xstrdup(name), *c;
        for (c = lc; *c; c++)
            if (*c >= 'A' && *c <= 'Z')
                *c = (char)(*c - 'A' + 'a');
        if (instruction(lc, q)) {
            free(lc);
            free(name);
            return;
        }
        free(lc);
    }
    fatal("知らない命令または擬似命令", name);
}

/* 行を文に分ける (; と # の注釈) */
static void processline(char *line)
{
    char *p = line, *start = line;
    int instr = 0, inchr = 0;
    for (;; p++) {
        if (instr) {
            if (*p == '\\' && p[1])
                p++;
            else if (*p == '"')
                instr = 0;
            else if (*p == 0)
                fatal("文字列が閉じていない", line);
            continue;
        }
        if (inchr) {
            inchr = 0;
            if (*p == '\\' && p[1])
                p++;
            continue;
        }
        if (*p == '"') {
            instr = 1;
            continue;
        }
        if (*p == '\'') {
            inchr = 1;
            continue;
        }
        if (*p == '#' || *p == 0 || *p == ';') {
            int end = *p != ';';
            *p = 0;
            process(start);
            if (end)
                break;
            start = p + 1;
        }
    }
}

static void runpass(char *text)
{
    char *p = text;
    int i;
    struct numlab *nl;
    char *buf = xmalloc(MAXLINE);
    for (nl = numlabs; nl; nl = nl->next)
        nl->cnt = 0;
    nsecstack = 0;
    nif = 0;
    altmacro = 0;
    localcnt = 0;
    pcrelcnt = 0;
    licnt = 0;
    lineno = 0;
    for (i = 0; i < nsec; i++) {
        secs[i].size = 0;
        secs[i].nrel = 0;
    }
    cur = getsec(".text", 0, 0, 0, 0);
    getsec(".data", 0, 0, 0, 0);
    getsec(".bss", 0, 0, 0, 0);
    prevsec = cur;
    while (*p) {
        char *e = strchr(p, '\n');
        long n = e ? e - p : (long)strlen(p);
        lineno++;
        if (n >= MAXLINE)
            fatal("行が長すぎる", 0);
        memcpy(buf, p, n);
        buf[n] = 0;
        processline(buf);
        p += n;
        if (*p == '\n')
            p++;
    }
    if (defining)
        fatal(".endm が無い", defining->name);
    if (nif)
        fatal(".endif が無い", 0);
    free(buf);
}

/* ---- ELF の出力 ---- */
struct strtab {
    char *buf;
    long size;
    long cap;
};

static long stradd(struct strtab *t, const char *s)
{
    long n = (long)strlen(s) + 1, off = t->size;
    while (t->size + n > t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->buf = xrealloc(t->buf, t->cap);
    }
    memcpy(t->buf + t->size, s, n);
    t->size += n;
    return off;
}

/* 出力はいったん記憶域に組み立てる (標準出力は戻って書き直せない) */
static unsigned char *obuf;
static long outpos;
static long ocap;

static void out1(int b)
{
    if (outpos + 1 > ocap) {
        ocap = ocap ? ocap * 2 : 65536;
        obuf = xrealloc(obuf, ocap);
    }
    obuf[outpos++] = (unsigned char)b;
}

static void outn(unsigned long v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        out1((int)(v & 0xff));
        v >>= 8;
    }
}

static void outbytes(const unsigned char *p, long n)
{
    long i;
    for (i = 0; i < n; i++)
        out1(p[i]);
}

static void outpad(long a)
{
    while (outpos % a)
        out1(0);
}

static void patch32(long at, unsigned long v)
{
    int i;
    for (i = 0; i < 4; i++) {
        obuf[at + i] = (unsigned char)(v & 0xff);
        v >>= 8;
    }
}

/* 別名を辿って (節, 値) を求める。未定義なら 0 */
static int resolve(struct sym *s, int *sec, long long *val)
{
    long long v = 0;
    int depth = 0;
    while (s->sec == S_ALIAS && s->alias && depth++ < 100) {
        v += s->val;
        s = s->alias;
    }
    if (s->sec == S_ALIAS || (s->defpass == 0 && s->type != STT_SECTION))
        return 0;
    *sec = s->sec;
    *val = v + (s->type == STT_SECTION ? 0 : s->val);
    return 1;
}

static int symcmp_order(struct sym *s)
{
    return s->bind == STB_LOCAL ? 0 : 1;
}

static int emitsym(struct sym *s)
{
    if (s->type == STT_SECTION)
        return 0;
    if (s->bind != STB_LOCAL)
        return s->defpass > 0 || s->used || s->sec == S_COMMON || s->bind == STB_WEAK;
    if (s->defpass == 0) {
        /* 未定義の局所記号への参照は大域の未定義にする */
        return s->used;
    }
    if (islocalname(s->name))
        return s->keep;
    return 1;
}

static void writeelf(void)
{
    struct strtab shstr, str;
    struct sym **list = 0;
    int nlist = 0, cap = 0, i, h, nlocal, shnum, symtabidx, strtabidx, shstridx;
    long *secoff, symoff, stroff, shoff;
    long *reloff;
    memset(&shstr, 0, sizeof shstr);
    memset(&str, 0, sizeof str);
    stradd(&shstr, "");
    stradd(&str, "");

    /* 未定義で参照された局所記号は大域にする */
    for (h = 0; h < 4096; h++) {
        struct sym *s;
        for (s = symtab[h]; s; s = s->next)
            if (s->defpass == 0 && s->used && s->bind == STB_LOCAL && s->sec != S_COMMON)
                s->bind = STB_GLOBAL;
    }

    /* 節の番号: 1.. が中身の節，続いて .rela.*，.symtab，.strtab，.shstrtab */
    shnum = 1;
    for (i = 0; i < nsec; i++) {
        secs[i].elfidx = shnum++;
        secs[i].nameoff = stradd(&shstr, secs[i].name);
    }
    for (i = 0; i < nsec; i++)
        if (secs[i].nrel) {
            char *rn = xmalloc((long)strlen(secs[i].name) + 7);
            sprintf(rn, ".rela%s", secs[i].name);
            secs[i].relidx = shnum++;
            secs[i].relnameoff = stradd(&shstr, rn);
            free(rn);
        }
    symtabidx = shnum++;
    strtabidx = shnum++;
    shstridx = shnum++;

    /* 記号の並び: 空・節の記号・局所・大域 */
    for (i = 0; i < nsec; i++) {
        if (nlist >= cap) {
            cap = cap ? cap * 2 : 256;
            list = xrealloc(list, (long)cap * (long)sizeof(struct sym *));
        }
        list[nlist++] = secs[i].ssym;
    }
    for (i = 0; i < 2; i++)
        for (h = 0; h < 4096; h++) {
            struct sym *s;
            for (s = symtab[h]; s; s = s->next)
                if (emitsym(s) && symcmp_order(s) == i) {
                    if (nlist >= cap) {
                        cap = cap ? cap * 2 : 256;
                        list = xrealloc(list, (long)cap * (long)sizeof(struct sym *));
                    }
                    list[nlist++] = s;
                }
        }
    nlocal = 1;
    for (i = 0; i < nlist; i++) {
        list[i]->elfidx = i + 1;
        if (list[i]->bind == STB_LOCAL)
            nlocal = i + 2;
    }

    /* 中身の配置 */
    outpos = 0;
    secoff = xmalloc((long)(nsec + 1) * (long)sizeof(long));
    reloff = xmalloc((long)(nsec + 1) * (long)sizeof(long));
    outn(0x464c457fUL, 4);           /* \x7fELF */
    out1(1);                          /* ELFCLASS32 */
    out1(1);                          /* ELFDATA2LSB */
    out1(1);                          /* EV_CURRENT */
    out1(0);                          /* ELFOSABI_NONE */
    outn(0, 4);
    outn(0, 4);
    outn(1, 2);                       /* ET_REL */
    outn(243, 2);                     /* EM_RISCV */
    outn(1, 4);
    outn(0, 4);                       /* e_entry */
    outn(0, 4);                       /* e_phoff */
    outn(0, 4);                       /* e_shoff (後で書く) */
    outn(0, 4);                       /* e_flags: soft-float，RVC なし */
    outn(52, 2);
    outn(0, 2);
    outn(0, 2);
    outn(40, 2);
    outn((unsigned long)shnum, 2);
    outn((unsigned long)shstridx, 2);

    for (i = 0; i < nsec; i++) {
        long a = secs[i].align > 4 ? secs[i].align : 4;
        outpad(a);
        secoff[i] = outpos;
        if (secs[i].type != SHT_NOBITS)
            outbytes(secs[i].data, secs[i].size);
    }
    for (i = 0; i < nsec; i++) {
        int k;
        if (!secs[i].nrel)
            continue;
        outpad(4);
        reloff[i] = outpos;
        for (k = 0; k < secs[i].nrel; k++) {
            struct rel *r = &secs[i].rel[k];
            outn((unsigned long)r->off, 4);
            outn(((unsigned long)r->sym->elfidx << 8) | (unsigned long)r->type, 4);
            outn((unsigned long)(r->addend & 0xffffffffLL), 4);
        }
    }
    outpad(4);
    symoff = outpos;
    outn(0, 4); outn(0, 4); outn(0, 4); out1(0); out1(0); outn(0, 2);
    for (i = 0; i < nlist; i++) {
        struct sym *s = list[i];
        unsigned long name = 0, value = 0, size = 0;
        int shndx;
        int sec = S_UNDEF;
        long long v = 0;
        if (s->sec == S_ALIAS && !resolve(s, &sec, &v))
            fatal("別名の相手が定義されていない", s->name);
        if (s->sec != S_ALIAS) {
            sec = s->sec;
            v = s->val;
        }
        if (s->type != STT_SECTION)
            name = (unsigned long)stradd(&str, s->name);
        if (s->type == STT_SECTION) {
            shndx = secs[s->sec].elfidx;
            value = 0;
        } else if (s->defpass == 0 && s->sec != S_COMMON && s->sec != S_ALIAS) {
            shndx = 0;
        } else if (sec == S_ABS) {
            shndx = SHN_ABS;
            value = (unsigned long)(v & 0xffffffffLL);
        } else if (sec == S_COMMON) {
            shndx = SHN_COMMON;
            value = (unsigned long)s->val;
        } else {
            shndx = secs[sec].elfidx;
            value = (unsigned long)(v & 0xffffffffLL);
        }
        if (s->has_size)
            size = (unsigned long)s->size;
        outn(name, 4);
        outn(value, 4);
        outn(size, 4);
        out1((s->bind << 4) | (s->type & 0xf));
        out1(s->hidden ? STV_HIDDEN : 0);
        outn((unsigned long)shndx, 2);
    }
    stroff = outpos;
    outbytes((unsigned char *)str.buf, str.size);
    {
        long a = stradd(&shstr, ".symtab"), b = stradd(&shstr, ".strtab"),
             c = stradd(&shstr, ".shstrtab");
        long base = outpos;
        outbytes((unsigned char *)shstr.buf, shstr.size);
        outpad(4);
        shoff = outpos;
        /* 節見出し */
        for (i = 0; i < 10; i++)
            outn(0, 4);
        for (i = 0; i < nsec; i++) {
            outn((unsigned long)secs[i].nameoff, 4);
            outn((unsigned long)secs[i].type, 4);
            outn(secs[i].flags, 4);
            outn(0, 4);
            outn((unsigned long)secoff[i], 4);
            outn((unsigned long)secs[i].size, 4);
            outn(0, 4);
            outn(0, 4);
            outn((unsigned long)secs[i].align, 4);
            outn((unsigned long)secs[i].entsize, 4);
        }
        for (i = 0; i < nsec; i++) {
            if (!secs[i].nrel)
                continue;
            outn((unsigned long)secs[i].relnameoff, 4);
            outn(SHT_RELA, 4);
            outn(SHF_INFO_LINK, 4);
            outn(0, 4);
            outn((unsigned long)reloff[i], 4);
            outn((unsigned long)secs[i].nrel * 12, 4);
            outn((unsigned long)symtabidx, 4);
            outn((unsigned long)secs[i].elfidx, 4);
            outn(4, 4);
            outn(12, 4);
        }
        outn((unsigned long)a, 4);
        outn(SHT_SYMTAB, 4);
        outn(0, 4);
        outn(0, 4);
        outn((unsigned long)symoff, 4);
        outn((unsigned long)(nlist + 1) * 16, 4);
        outn((unsigned long)strtabidx, 4);
        outn((unsigned long)nlocal, 4);
        outn(4, 4);
        outn(16, 4);
        outn((unsigned long)b, 4);
        outn(SHT_STRTAB, 4);
        outn(0, 4);
        outn(0, 4);
        outn((unsigned long)stroff, 4);
        outn((unsigned long)str.size, 4);
        outn(0, 4);
        outn(0, 4);
        outn(1, 4);
        outn(0, 4);
        outn((unsigned long)c, 4);
        outn(SHT_STRTAB, 4);
        outn(0, 4);
        outn(0, 4);
        outn((unsigned long)base, 4);
        outn((unsigned long)shstr.size, 4);
        outn(0, 4);
        outn(0, 4);
        outn(1, 4);
        outn(0, 4);
    }
    patch32(32, (unsigned long)shoff);
}

/* ---- 入力 ---- */
static char *readall(FILE *f)
{
    long cap = 65536, n = 0;
    char *buf = xmalloc(cap);
    int c;
    while ((c = getc(f)) != EOF) {
        /* stone の pp / cc の慣習の 0x04 は入力の終わり */
        if (c == 4)
            break;
        if (n + 2 > cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[n++] = (char)c;
    }
    buf[n] = 0;
    return buf;
}

int main(int argc, char **argv)
{
    const char *outname = 0, *inname = 0;
    char *text;
    FILE *in;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outname = argv[++i];
        else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            if (nincdir < 64)
                incdirs[nincdir++] = argv[++i];
        } else if (strncmp(argv[i], "-I", 2) == 0) {
            if (nincdir < 64)
                incdirs[nincdir++] = argv[i] + 2;
        } else if (argv[i][0] == '-' && argv[i][1])
            ;   /* -march= / -mabi= / -mno-relax などは無視する */
        else if (strcmp(argv[i], "-") == 0)
            inname = 0;
        else
            inname = argv[i];
    }
    if (inname) {
        in = fopen(inname, "r");
        if (!in) {
            fprintf(stderr, "nas1: 開けない: %s\n", inname);
            return 1;
        }
        infile = inname;
    } else
        in = stdin;
    text = readall(in);
    if (in != stdin)
        fclose(in);
    pass = 1;
    runpass(text);
    pass = 2;
    runpass(text);
    writeelf();
    {
        FILE *outf = stdout;
        if (outname) {
            outf = fopen(outname, "wb");
            if (!outf) {
                fprintf(stderr, "nas1: 書けない: %s\n", outname);
                return 1;
            }
        }
        if ((long)fwrite(obuf, 1, outpos, outf) != outpos) {
            fprintf(stderr, "nas1: 書込みに失敗した\n");
            return 1;
        }
        if (outf != stdout)
            fclose(outf);
        else
            fflush(stdout);
    }
    return 0;
}
