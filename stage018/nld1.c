/* nld1.c --- ELF32 / RISC-V のリンカ (docs/stage018-ncc.md 6.2)
 *
 *   nld1 [-o <出力>] [-T <台本>] [-e <入口>] <入力.o>...
 *
 * nas1 が出す ELF32 の再配置可能オブジェクトを結合し，実行形式 (ET_EXEC) を
 * 出す。番地の割付けはリンカ台本 (GNU ld の書式の部分集合) で決める。-T を
 * 省くと，QEMU virt の RAM の先頭 (0x8000_0000) から .text・.rodata・.data・
 * .bss を並べ，その後ろに 64 KiB のスタックを置く既定の台本 (deftext) を使う。
 * 既定の台本も同じ解析器で読む。
 *
 * 緩和はしない。R_RISCV_RELAX は読み捨て，R_RISCV_ALIGN は誤りとする
 * (nas1 はどちらも出さない)。
 *
 * 台本が置かない節 (orphan) は，同じ種類 (命令・読出し専用・書込み可・
 * nobits) の最後の出力節の後ろに，同じ名前の出力節として置く。割り付けない
 * 節 (.comment など) は出力に含めない。
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む (同 5.1)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ELF の定数 ---- */
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define R_RISCV_NONE 0
#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_BRANCH 16
#define R_RISCV_JAL 17
#define R_RISCV_CALL 18
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
#define R_RISCV_ALIGN 43
#define R_RISCV_RELAX 51

typedef unsigned long u32;      /* 32 bit の値を入れる (ホストでは 64 bit でもよい) */

static const char *deftext =
    "ENTRY(_start)\n"
    "SECTIONS {\n"
    "  . = 0x80000000;\n"
    "  .text : { *(.text.init) *(.text .text.*) }\n"
    "  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }\n"
    "  .data : { *(.data .data.* .sdata .sdata.*) }\n"
    "  .bss : { __bss_start = .; *(.sbss .sbss.* .bss .bss.*) *(COMMON) __bss_end = .; }\n"
    "  _end = .;\n"
    "  . = ALIGN(16) + 0x10000;\n"
    "  __stack_top = .;\n"
    "}\n";

/* ---- 入力 ---- */
struct isec;
struct gsym;

struct lsym {                   /* 入力ファイルの記号表の 1 項 */
    char *name;
    int bind;
    int type;
    int shndx;
    u32 value;
    u32 size;
    struct gsym *g;             /* 大域・weak なら名前で引いた大域の記号 */
};

struct ifile {
    char *name;
    unsigned char *data;
    long size;
    int nsec;
    struct isec **secs;         /* 節の番号から (割り付けないものは 0) */
    int nsym;
    struct lsym *syms;
};

struct osec;

struct isec {
    struct ifile *file;
    int idx;
    char *name;
    int type;
    u32 flags;
    u32 align;
    u32 size;
    unsigned char *data;        /* NOBITS なら 0 */
    unsigned char *rel;         /* 対応する .rela の中身 */
    int nrel;
    struct osec *out;
    u32 addr;                   /* 割り付けた番地 (VMA) */
    int discard;
    int assigned;
};

struct gsym {
    char *name;
    struct gsym *next;
    int defined;                /* 1: 入力の定義，2: 台本の定義 */
    int weak;
    int referenced;
    struct isec *sec;           /* 入力の定義の節 (0 なら絶対値) */
    u32 value;
    u32 size;
    int type;
    u32 common;                 /* COMMON の大きさ (0 でなければ COMMON) */
    u32 calign;
    struct osec *osec;          /* 台本の定義を置いた出力節 */
    int provide;                /* PROVIDE で 1 */
    int hidden;
    int outidx;                 /* 出力の記号表の番号 (-r) */
};

/* ---- 台本 ---- */
#define E_NUM 1
#define E_SYM 2
#define E_DOT 3
#define E_UN 4
#define E_BIN 5
#define E_COND 6
#define E_FUNC 7

struct expr {
    int kind;
    int op;
    long long num;
    char *name;
    struct expr *a;
    struct expr *b;
    struct expr *c;
};

#define S_ASSIGN 1
#define S_OSEC 2
#define S_INPUT 3
#define S_DATA 4
#define S_ASSERT 5

struct stmt {
    int kind;
    struct stmt *next;
    /* S_ASSIGN */
    char *sym;
    int provide;
    int hidden;
    struct expr *e;
    /* S_OSEC */
    struct osec *os;
    /* S_INPUT */
    char *filepat;
    int nsecpat;
    char **secpat;
    int nexcl;
    char **excl;
    int sort;
    int nlist;
    struct isec **list;
    /* S_DATA */
    int dsize;
    /* S_ASSERT */
    char *msg;
};

struct osec {
    char *name;
    struct expr *addr;
    struct expr *at;
    struct expr *align;
    int noload;
    int discard;
    struct stmt *body;
    struct osec *next;          /* 出力の並び */
    u32 vma;
    u32 lma;
    u32 size;
    u32 maxalign;
    u32 flags;
    int type;
    int kind;                   /* 0 命令，1 読出し，2 書込み，3 nobits，-1 空 */
    int orphan;
    int nph;                    /* :phdr で指定した (または前から引き継いだ) program header */
    char **phname;              /* 台本に書いた名前 */
    struct phdr **ph;           /* 名前を PHDRS で引いたもの */
    unsigned char *data;
    int shndx;
    long offset;
    long nameoff;
    struct osec *link;          /* 節見出しの sh_link (動的な節) */
    u32 info;
    u32 entsize;
};

/* PHDRS の 1 項 */
struct phdr {
    char *name;
    u32 type;
    int filehdr;
    int phdrs;
    struct expr *flagexpr;      /* FLAGS(...) (無ければ節の旗から決める) */
    struct phdr *next;
    /* 出力の時に決めるもの */
    u32 offset, vaddr, paddr, filesz, memsz, align, flags;
    int nsec;
};

#define PT_DYNAMIC 2
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_GNU_EH_FRAME 0x6474e550UL
#define PT_GNU_STACK 0x6474e551UL

/* ---- 状態 ---- */
static struct ifile **files;
static int nfiles;
static struct gsym *gtab[4096];
static struct stmt *top;
static char *entryname;
static struct osec *cursec;     /* 割付け中の出力節 */
static u32 dot;
static int lpass;
static int final;
static struct ifile *commonfile;
static int stripall;            /* -s: 記号表を出さない */
static int buildid;             /* --build-id */
static int shared;              /* -shared */
static const char *soname;
static struct phdr *phdrs;      /* PHDRS (無ければ 0) */
static int nphdrs;
static char *vername;           /* VERSION の節点の名前 (1 つだけ持つ) */
static char **verglobal;        /* その global: の記号 */
static int nverglobal;

static void fatal(const char *msg, const char *arg)
{
    if (arg)
        fprintf(stderr, "nld1: %s: %s\n", msg, arg);
    else
        fprintf(stderr, "nld1: %s\n", msg);
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p)
        fatal("記憶域が足りない", 0);
    memset(p, 0, n > 0 ? n : 1);
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

static u32 rd16(const unsigned char *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 rd32(const unsigned char *p)
{
    return ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24))
        & 0xffffffffUL;
}

static void wr32(unsigned char *p, u32 v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static u32 alignup(u32 v, u32 a)
{
    if (a <= 1)
        return v;
    return ((v + a - 1) / a * a) & 0xffffffffUL;
}

/* ---- 大域の記号 ---- */
static unsigned long hashstr(const char *s)
{
    unsigned long h = 2166136261UL;
    while (*s) {
        h = (h ^ (unsigned char)*s++) * 16777619UL;
        h &= 0xffffffffUL;
    }
    return h;
}

static struct gsym *gsym(const char *name, int create)
{
    unsigned long h = hashstr(name) % 4096;
    struct gsym *g;
    for (g = gtab[h]; g; g = g->next)
        if (strcmp(g->name, name) == 0)
            return g;
    if (!create)
        return 0;
    g = xmalloc(sizeof(struct gsym));
    g->name = xstrdup(name);
    g->next = gtab[h];
    gtab[h] = g;
    return g;
}

/* ---- 入力の読込み ---- */
static unsigned char *readfile(const char *name, long *size)
{
    FILE *f = fopen(name, "rb");
    long cap = 65536, n = 0;
    unsigned char *buf;
    int c;
    if (!f)
        fatal("開けない", name);
    buf = xmalloc(cap);
    while ((c = getc(f)) != EOF) {
        if (n >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[n++] = (unsigned char)c;
    }
    fclose(f);
    *size = n;
    return buf;
}

/* 読み込んだオブジェクトを結合に加える。data は f が持つ */
static void readobj_mem(const char *name, unsigned char *data, long size)
{
    struct ifile *f = xmalloc(sizeof(struct ifile));
    unsigned char *d, *sh, *shstr;
    u32 shoff, shnum, shstrndx, i;
    int symsec = -1;
    f->name = xstrdup(name);
    f->data = d = data;
    f->size = size;
    if (f->size < 52 || d[0] != 0x7f || d[1] != 'E' || d[2] != 'L' || d[3] != 'F')
        fatal("ELF でない", name);
    if (d[4] != 1 || d[5] != 1)
        fatal("ELF32 の little endian でない", name);
    if (rd16(d + 16) != 1)
        fatal("再配置可能オブジェクト (ET_REL) でない", name);
    if (rd16(d + 18) != 243)
        fatal("RISC-V のオブジェクトでない", name);
    shoff = rd32(d + 32);
    shnum = rd16(d + 48);
    shstrndx = rd16(d + 50);
    if (shoff + shnum * 40 > (u32)f->size)
        fatal("節見出しが末尾を越える", name);
    sh = d + shoff;
    shstr = d + rd32(sh + shstrndx * 40 + 16);
    f->nsec = (int)shnum;
    f->secs = xmalloc((long)shnum * (long)sizeof(struct isec *));
    for (i = 0; i < shnum; i++) {
        unsigned char *h = sh + i * 40;
        u32 type = rd32(h + 4), flags = rd32(h + 8);
        struct isec *s;
        if (type == SHT_SYMTAB)
            symsec = (int)i;
        if (!(flags & SHF_ALLOC) || (type != SHT_PROGBITS && type != SHT_NOBITS
                                     && type != 7 && type != 14 && type != 15 && type != 16))
            continue;
        s = xmalloc(sizeof(struct isec));
        s->file = f;
        s->idx = (int)i;
        s->name = (char *)shstr + rd32(h);
        s->type = (int)type;
        s->flags = flags;
        s->align = rd32(h + 32) ? rd32(h + 32) : 1;
        s->size = rd32(h + 20);
        if (type != SHT_NOBITS) {
            if (rd32(h + 16) + s->size > (u32)f->size)
                fatal("節の中身が末尾を越える", name);
            s->data = d + rd32(h + 16);
        }
        f->secs[i] = s;
    }
    for (i = 0; i < shnum; i++) {
        unsigned char *h = sh + i * 40;
        u32 info = rd32(h + 28);
        if (rd32(h + 4) != SHT_RELA)
            continue;
        if (info < shnum && f->secs[info]) {
            f->secs[info]->rel = d + rd32(h + 16);
            f->secs[info]->nrel = (int)(rd32(h + 20) / 12);
        }
    }
    if (symsec >= 0) {
        unsigned char *h = sh + symsec * 40;
        unsigned char *st = d + rd32(h + 16);
        unsigned char *str = d + rd32(sh + rd32(h + 24) * 40 + 16);
        int n = (int)(rd32(h + 20) / 16), k;
        f->nsym = n;
        f->syms = xmalloc((long)n * (long)sizeof(struct lsym));
        for (k = 0; k < n; k++) {
            unsigned char *e = st + k * 16;
            struct lsym *ls = &f->syms[k];
            ls->name = (char *)str + rd32(e);
            ls->value = rd32(e + 4);
            ls->size = rd32(e + 8);
            ls->bind = e[12] >> 4;
            ls->type = e[12] & 0xf;
            ls->shndx = (int)rd16(e + 14);
            if (k > 0 && ls->bind != STB_LOCAL) {
                struct gsym *g = gsym(ls->name, 1);
                ls->g = g;
                if ((e[13] & 3) == 2)
                    g->hidden = 1;
                if (ls->shndx == SHN_UNDEF) {
                    g->referenced = 1;
                    continue;
                }
                if (ls->shndx == SHN_COMMON) {
                    /* 強い定義は COMMON に勝ち，COMMON は weak の定義に勝つ */
                    if (g->defined && !g->weak)
                        continue;
                    if (g->defined) {
                        g->defined = 0;
                        g->weak = 0;
                        g->sec = 0;
                    }
                    if (ls->size > g->common)
                        g->common = ls->size;
                    if (ls->value > g->calign)
                        g->calign = ls->value;
                    continue;
                }
                if (g->defined && !g->weak && ls->bind != STB_WEAK)
                    fatal("記号の多重定義", ls->name);
                if ((g->defined || g->common) && ls->bind == STB_WEAK)
                    continue;
                g->defined = 1;
                g->weak = ls->bind == STB_WEAK;
                g->common = 0;
                g->type = ls->type;
                g->size = ls->size;
                g->value = ls->value;
                if (ls->shndx == SHN_ABS)
                    g->sec = 0;
                else if (ls->shndx < f->nsec && f->secs[ls->shndx])
                    g->sec = f->secs[ls->shndx];
                else
                    fatal("記号が割り付けない節にある", ls->name);
            }
        }
    }
    files = xrealloc(files, (long)(nfiles + 1) * (long)sizeof(struct ifile *));
    files[nfiles++] = f;
}

/* ---- 書庫 ----
 * --whole-archive の中の書庫はメンバをすべて読む。それ以外の書庫は，読んだ
 * オブジェクトが参照して定義の無い大域の記号を定義するメンバだけを読み，
 * 新しく読むものが無くなるまで繰り返す (書庫どうしの順を問わない。
 * --start-group / --end-group はこれに含まれる)。 */
struct amember {
    char *name;
    unsigned char *data;
    long size;
    int loaded;
};

struct archive {
    char *name;
    int n;
    struct amember *m;
    struct archive *next;
};

static struct archive *archives, **archtail = &archives;
static int whole;               /* --whole-archive の中 */

static long arfield(const unsigned char *h, int off, int len)
{
    char b[16];
    memcpy(b, h + off, len);
    b[len] = 0;
    return strtol(b, 0, 10);
}

/* 書庫のメンバを並べる。thin ならメンバのファイルを書庫の場所から読む */
static struct archive *readarchive(const char *name, unsigned char *p, long size)
{
    struct archive *a = xmalloc(sizeof(struct archive));
    int thin = memcmp(p, "!<thin>\n", 8) == 0;
    long off = 8, nnames = 0;
    const char *names = 0, *slash = strrchr(name, '/');
    long dirlen = slash ? (long)(slash - name) + 1 : 0;
    a->name = xstrdup(name);
    while (off + 60 <= size) {
        const unsigned char *h = p + off;
        long msize = arfield(h, 48, 10);
        int special = h[0] == '/' && (h[1] == ' ' || (h[1] == '/' && h[2] == ' '));
        if (h[58] != '`' || h[59] != '\n')
            fatal("書庫のメンバの頭が壊れている", name);
        if (special && h[1] == '/') {
            names = (const char *)(p + off + 60);
            nnames = msize;
        }
        if (!special) {
            char *mname;
            struct amember *m;
            long k = 0;
            if (h[0] == '/') {
                long no = arfield(h, 1, 15);
                if (!names || no >= nnames)
                    fatal("書庫に長い名前の表が無い", name);
                while (no + k < nnames && names[no + k] != '\n')
                    k++;
                if (k > 0 && names[no + k - 1] == '/')
                    k--;
                mname = xstrndup(names + no, k);
            } else {
                while (k < 16 && h[k] != '/' && h[k] != ' ')
                    k++;
                mname = xstrndup((const char *)h, k);
            }
            a->m = xrealloc(a->m, (long)(a->n + 1) * (long)sizeof(struct amember));
            m = &a->m[a->n++];
            memset(m, 0, sizeof *m);
            if (thin) {
                char *path = xmalloc(dirlen + (long)strlen(mname) + 1);
                if (mname[0] != '/')
                    memcpy(path, name, dirlen);
                strcpy(path + (mname[0] != '/' ? dirlen : 0), mname);
                m->name = path;
                m->data = readfile(path, &m->size);
            } else {
                if (off + 60 + msize > size)
                    fatal("書庫のメンバが末尾を越える", name);
                m->name = xmalloc((long)strlen(name) + (long)strlen(mname) + 3);
                sprintf(m->name, "%s(%s)", name, mname);
                m->data = xmalloc(msize);
                memcpy(m->data, p + off + 60, msize);
                m->size = msize;
            }
        }
        off += 60;
        if (!thin || special)
            off += msize + (msize & 1);
    }
    return a;
}

static void readobj(const char *name)
{
    long size;
    unsigned char *p = readfile(name, &size);
    if (size >= 8 && (memcmp(p, "!<arch>\n", 8) == 0 || memcmp(p, "!<thin>\n", 8) == 0)) {
        struct archive *a = readarchive(name, p, size);
        int i;
        free(p);
        if (whole) {
            for (i = 0; i < a->n; i++) {
                a->m[i].loaded = 1;
                readobj_mem(a->m[i].name, a->m[i].data, a->m[i].size);
            }
            return;
        }
        *archtail = a;
        archtail = &a->next;
        return;
    }
    readobj_mem(name, p, size);
}

/* メンバが，参照されて定義の無い大域の記号を定義するか */
static int member_needed(struct amember *m)
{
    const unsigned char *d = m->data;
    u32 shoff, shnum, i;
    if (m->size < 52 || memcmp(d, "\177ELF", 4) != 0)
        return 0;
    shoff = rd32(d + 32);
    shnum = rd16(d + 48);
    if (shoff + shnum * 40 > (u32)m->size)
        fatal("節見出しが末尾を越える", m->name);
    for (i = 0; i < shnum; i++) {
        const unsigned char *h = d + shoff + i * 40;
        u32 off, n, k;
        const char *str;
        if (rd32(h + 4) != SHT_SYMTAB)
            continue;
        off = rd32(h + 16);
        n = rd32(h + 20) / 16;
        str = (const char *)d + rd32(d + shoff + rd32(h + 24) * 40 + 16);
        for (k = 1; k < n; k++) {
            const unsigned char *e = d + off + k * 16;
            struct gsym *g;
            if ((e[12] >> 4) == STB_LOCAL || rd16(e + 14) == SHN_UNDEF || rd16(e + 14) == SHN_COMMON)
                continue;
            g = gsym(str + rd32(e), 0);
            if (g && g->referenced && !g->defined && !g->common)
                return 1;
        }
    }
    return 0;
}

static void pull_members(void)
{
    int changed = 1;
    while (changed) {
        struct archive *a;
        changed = 0;
        for (a = archives; a; a = a->next) {
            int i;
            for (i = 0; i < a->n; i++)
                if (!a->m[i].loaded && member_needed(&a->m[i])) {
                    a->m[i].loaded = 1;
                    readobj_mem(a->m[i].name, a->m[i].data, a->m[i].size);
                    changed = 1;
                }
        }
    }
}

/* COMMON の記号を，擬似ファイル "COMMON" の節に 1 つずつ置く */
static void makecommon(void)
{
    int h, n = 0;
    struct gsym *g;
    commonfile = xmalloc(sizeof(struct ifile));
    commonfile->name = "COMMON";
    for (h = 0; h < 4096; h++)
        for (g = gtab[h]; g; g = g->next)
            if (g->common && !g->defined)
                n++;
    commonfile->secs = xmalloc((long)(n + 1) * (long)sizeof(struct isec *));
    n = 0;
    for (h = 0; h < 4096; h++)
        for (g = gtab[h]; g; g = g->next)
            if (g->common && !g->defined) {
                struct isec *s = xmalloc(sizeof(struct isec));
                s->file = commonfile;
                s->idx = n;
                s->name = "COMMON";
                s->type = SHT_NOBITS;
                s->flags = SHF_ALLOC | SHF_WRITE;
                s->align = g->calign ? g->calign : 4;
                s->size = g->common;
                commonfile->secs[n++] = s;
                g->defined = 1;
                g->sec = s;
                g->value = 0;
                g->size = g->common;
                g->type = STT_OBJECT;
            }
    commonfile->nsec = n;
}

/* ---- 台本の字句 ---- */
static const char *sp;
static const char *scriptname;

static void serr(const char *msg)
{
    char ctx[40];
    long n = 0;
    while (sp[n] && sp[n] != '\n' && n < 30)
        n++;
    memcpy(ctx, sp, n);
    ctx[n] = 0;
    fprintf(stderr, "nld1: %s: %s (残り: %s)\n", scriptname, msg, ctx);
    exit(1);
}

static void skipws(void)
{
    for (;;) {
        while (*sp == ' ' || *sp == '\t' || *sp == '\n' || *sp == '\r' || *sp == '\f')
            sp++;
        if (sp[0] == '/' && sp[1] == '*') {
            const char *e = strstr(sp + 2, "*/");
            if (!e)
                serr("注釈が閉じていない");
            sp = e + 2;
            continue;
        }
        break;
    }
}

static int peekc(void)
{
    skipws();
    return *sp;
}

static int accept(int c)
{
    if (peekc() == c) {
        sp++;
        return 1;
    }
    return 0;
}

static void expect(int c)
{
    char m[32];
    if (!accept(c)) {
        sprintf(m, "'%c' が要る", c);
        serr(m);
    }
}

static int isnamec(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '.' || c == '$';
}

/* 式の中の名前 */
static char *exprname(void)
{
    const char *s;
    skipws();
    s = sp;
    if (*sp == '"') {
        sp++;
        while (*sp && *sp != '"')
            sp++;
        if (!*sp)
            serr("文字列が閉じていない");
        sp++;
        return xstrndup(s + 1, sp - s - 2);
    }
    while (isnamec(*sp))
        sp++;
    if (sp == s)
        serr("名前が要る");
    return xstrndup(s, sp - s);
}

/* 入力節の指定の中の名前 (ワイルドカードを含む) */
static char *patname(void)
{
    const char *s;
    skipws();
    s = sp;
    if (*sp == '"')
        return exprname();
    while (*sp && !strchr(" \t\r\n(),;{}=", *sp))
        sp++;
    if (sp == s)
        serr("名前が要る");
    return xstrndup(s, sp - s);
}

static int keyword(const char *kw)
{
    long n = (long)strlen(kw);
    skipws();
    if (strncmp(sp, kw, n) == 0 && !isnamec(sp[n])) {
        sp += n;
        return 1;
    }
    return 0;
}

/* ---- 台本の式 ---- */
static struct expr *mkexpr(int kind)
{
    struct expr *e = xmalloc(sizeof(struct expr));
    e->kind = kind;
    return e;
}

static struct expr *parse_expr(void);

static long long parse_number(void)
{
    long long v = 0;
    int base = 10;
    if (sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
        base = 16;
        sp += 2;
    }
    for (;;) {
        int c = *sp, d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = v * base + d;
        sp++;
    }
    if (*sp == 'K' || *sp == 'k') {
        v *= 1024;
        sp++;
    } else if (*sp == 'M' || *sp == 'm') {
        v *= 1024 * 1024;
        sp++;
    }
    return v;
}

static const char *funcs[] = {
    "ALIGN", "ADDR", "LOADADDR", "SIZEOF", "DEFINED", "MAX", "MIN", "ABSOLUTE",
    "CONSTANT", "ALIGNOF", "LOG2CEIL", "SIZEOF_HEADERS", "NEXT", 0
};

static struct expr *primary(void)
{
    struct expr *e;
    int c = peekc();
    if (c == '(') {
        sp++;
        e = parse_expr();
        expect(')');
        return e;
    }
    if (c == '-' || c == '~' || c == '!') {
        sp++;
        e = mkexpr(E_UN);
        e->op = c;
        e->a = primary();
        return e;
    }
    if (c >= '0' && c <= '9') {
        e = mkexpr(E_NUM);
        e->num = parse_number();
        return e;
    }
    if (c == '.' && !isnamec(sp[1])) {
        sp++;
        return mkexpr(E_DOT);
    }
    {
        char *n = exprname();
        int i;
        for (i = 0; funcs[i]; i++)
            if (strcmp(n, funcs[i]) == 0)
                break;
        if (funcs[i]) {
            e = mkexpr(E_FUNC);
            e->name = n;
            if (strcmp(n, "SIZEOF_HEADERS") == 0)
                return e;
            expect('(');
            if (strcmp(n, "ADDR") == 0 || strcmp(n, "LOADADDR") == 0
                || strcmp(n, "SIZEOF") == 0 || strcmp(n, "DEFINED") == 0
                || strcmp(n, "ALIGNOF") == 0 || strcmp(n, "CONSTANT") == 0) {
                struct expr *a = mkexpr(E_SYM);
                a->name = patname();
                e->a = a;
            } else {
                e->a = parse_expr();
                if (accept(','))
                    e->b = parse_expr();
            }
            expect(')');
            return e;
        }
        e = mkexpr(E_SYM);
        e->name = n;
        return e;
    }
}

/* 2 項演算子。優先順位は C と同じ */
static int binprec(int *op, int *len)
{
    const char *p;
    skipws();
    p = sp;
    *len = 2;
    if (p[0] == '|' && p[1] == '|') { *op = 'O'; return 1; }
    if (p[0] == '&' && p[1] == '&') { *op = 'A'; return 2; }
    if (p[0] == '=' && p[1] == '=') { *op = 'E'; return 6; }
    if (p[0] == '!' && p[1] == '=') { *op = 'N'; return 6; }
    if (p[0] == '<' && p[1] == '=') { *op = 'l'; return 7; }
    if (p[0] == '>' && p[1] == '=') { *op = 'g'; return 7; }
    if (p[0] == '<' && p[1] == '<' && p[2] != '=') { *op = 'L'; return 8; }
    if (p[0] == '>' && p[1] == '>' && p[2] != '=') { *op = 'R'; return 8; }
    *len = 1;
    if (p[1] == '=')
        return 0;               /* 複合代入は式の外 */
    switch (p[0]) {
    case '|': *op = '|'; return 3;
    case '^': *op = '^'; return 4;
    case '&': *op = '&'; return 5;
    case '<': *op = '<'; return 7;
    case '>': *op = '>'; return 7;
    case '+': *op = '+'; return 9;
    case '-': *op = '-'; return 9;
    case '*': *op = '*'; return 10;
    case '/': *op = '/'; return 10;
    case '%': *op = '%'; return 10;
    }
    return 0;
}

static struct expr *binexpr(int minprec)
{
    struct expr *l = primary();
    for (;;) {
        int op, len, p = binprec(&op, &len);
        struct expr *e;
        if (p == 0 || p < minprec)
            return l;
        sp += len;
        e = mkexpr(E_BIN);
        e->op = op;
        e->a = l;
        e->b = binexpr(p + 1);
        l = e;
    }
}

static struct expr *parse_expr(void)
{
    struct expr *c = binexpr(1);
    if (accept('?')) {
        struct expr *e = mkexpr(E_COND);
        e->a = c;
        e->b = parse_expr();
        expect(':');
        e->c = parse_expr();
        return e;
    }
    return c;
}

/* ---- 台本の文 ---- */
static struct stmt *mkstmt(int kind)
{
    struct stmt *s = xmalloc(sizeof(struct stmt));
    s->kind = kind;
    return s;
}

/* sym = e; / sym += e; などを読む。sym は読んだ後 */
static struct stmt *assignment(char *sym)
{
    struct stmt *s = mkstmt(S_ASSIGN);
    int op = 0;
    skipws();
    if (sp[0] != '=' && sp[1] == '=') {
        op = sp[0];
        sp += 2;
    } else if (sp[0] == '<' && sp[1] == '<' && sp[2] == '=') {
        op = 'L';
        sp += 3;
    } else if (sp[0] == '>' && sp[1] == '>' && sp[2] == '=') {
        op = 'R';
        sp += 3;
    } else
        expect('=');
    s->sym = sym;
    s->e = parse_expr();
    if (op) {
        struct expr *b = mkexpr(E_BIN), *l;
        if (strcmp(sym, ".") == 0)
            l = mkexpr(E_DOT);
        else {
            l = mkexpr(E_SYM);
            l->name = sym;
        }
        b->op = op;
        b->a = l;
        b->b = s->e;
        s->e = b;
    }
    return s;
}

/* PROVIDE(sym = e) / PROVIDE_HIDDEN / HIDDEN */
static struct stmt *provide(int prov, int hid)
{
    struct stmt *s;
    expect('(');
    s = assignment(exprname());
    s->provide = prov;
    s->hidden = hid;
    expect(')');
    return s;
}

static void addpat(struct stmt *s, char *p)
{
    s->secpat = xrealloc(s->secpat, (long)(s->nsecpat + 1) * (long)sizeof(char *));
    s->secpat[s->nsecpat++] = p;
}

/* filepat ( secpat ... ) */
static struct stmt *inputspec(char *filepat)
{
    struct stmt *s = mkstmt(S_INPUT);
    s->filepat = filepat;
    expect('(');
    while (!accept(')')) {
        if (keyword("EXCLUDE_FILE")) {
            expect('(');
            while (!accept(')')) {
                s->excl = xrealloc(s->excl, (long)(s->nexcl + 1) * (long)sizeof(char *));
                s->excl[s->nexcl++] = patname();
            }
            continue;
        }
        if (keyword("SORT") || keyword("SORT_BY_NAME") || keyword("SORT_BY_ALIGNMENT")
            || keyword("SORT_BY_INIT_PRIORITY") || keyword("SORT_NONE")) {
            expect('(');
            s->sort = 1;
            while (!accept(')'))
                addpat(s, patname());
            continue;
        }
        addpat(s, patname());
        accept(',');
    }
    return s;
}

static void append(struct stmt ***tail, struct stmt *s)
{
    **tail = s;
    *tail = &s->next;
}

static struct stmt *section_body(void)
{
    struct stmt *head = 0, **tail = &head;
    expect('{');
    while (!accept('}')) {
        int c = peekc();
        if (c == ';') {
            sp++;
            continue;
        }
        if (keyword("KEEP")) {
            /* KEEP(*(.x)) と KEEP(SORT(*)(.x)) の形。gc をしないので KEEP は意味を持たない */
            struct stmt *in;
            expect('(');
            if (keyword("SORT") || keyword("SORT_BY_NAME")) {
                char *fp;
                expect('(');
                fp = patname();
                expect(')');
                in = inputspec(fp);
                in->sort = 1;
            } else
                in = inputspec(patname());
            append(&tail, in);
            expect(')');
            continue;
        }
        if (keyword("PROVIDE")) { append(&tail, provide(1, 0)); continue; }
        if (keyword("PROVIDE_HIDDEN")) { append(&tail, provide(1, 1)); continue; }
        if (keyword("HIDDEN")) { append(&tail, provide(0, 1)); continue; }
        if (keyword("BYTE") || keyword("SHORT") || keyword("LONG") || keyword("QUAD")) {
            struct stmt *s = mkstmt(S_DATA);
            const char *k = sp - 1;
            s->dsize = *k == 'E' ? 1 : *k == 'T' ? 2 : *k == 'G' ? 4 : 8;
            expect('(');
            s->e = parse_expr();
            expect(')');
            append(&tail, s);
            continue;
        }
        if (keyword("ASSERT")) {
            struct stmt *s = mkstmt(S_ASSERT);
            expect('(');
            s->e = parse_expr();
            expect(',');
            s->msg = exprname();
            expect(')');
            append(&tail, s);
            continue;
        }
        if (keyword("CONSTRUCTORS") || keyword("CREATE_OBJECT_SYMBOLS"))
            continue;
        if (keyword("FILL")) {
            expect('(');
            parse_expr();
            expect(')');
            continue;
        }
        {
            /* 代入か入力節の指定か。名前の後ろが '(' なら入力節 */
            const char *save = sp;
            char *n = patname();
            skipws();
            if (*sp == '(') {
                append(&tail, inputspec(n));
                continue;
            }
            sp = save;
            if (peekc() == '.' && !isnamec(sp[1])) {
                sp++;
                n = xstrdup(".");
            } else
                n = exprname();
            append(&tail, assignment(n));
            accept(';');
        }
    }
    return head;
}

static struct osec *new_osec(const char *name)
{
    struct osec *o = xmalloc(sizeof(struct osec));
    o->name = xstrdup(name);
    o->kind = -1;
    o->maxalign = 1;
    return o;
}

static struct stmt *output_section(char *name)
{
    struct stmt *s = mkstmt(S_OSEC);
    struct osec *o = new_osec(name);
    s->os = o;
    o->discard = strcmp(name, "/DISCARD/") == 0;
    /* [addr] [(type)] : */
    if (peekc() != ':') {
        if (peekc() == '(') {
            const char *save = sp;
            sp++;
            if (keyword("NOLOAD") || keyword("COPY") || keyword("INFO") || keyword("OVERLAY")
                || keyword("DSECT")) {
                o->noload = 1;
                expect(')');
            } else {
                sp = save;
                o->addr = parse_expr();
            }
        } else
            o->addr = parse_expr();
        if (peekc() == '(') {
            sp++;
            if (keyword("NOLOAD") || keyword("COPY") || keyword("INFO"))
                o->noload = 1;
            expect(')');
        }
    }
    expect(':');
    for (;;) {
        if (keyword("AT")) {
            expect('(');
            o->at = parse_expr();
            expect(')');
        } else if (keyword("ALIGN")) {
            expect('(');
            o->align = parse_expr();
            expect(')');
        } else if (keyword("SUBALIGN")) {
            expect('(');
            parse_expr();
            expect(')');
        } else
            break;
    }
    o->body = section_body();
    /* > region，AT> region，:phdr，= fill を読み捨てる */
    for (;;) {
        skipws();
        if (*sp == '>') {
            sp++;
            exprname();
        } else if (sp[0] == 'A' && sp[1] == 'T' && sp[2] == '>') {
            sp += 3;
            exprname();
        } else if (*sp == ':' && sp[1] != ':') {
            /* :phdr。PHDRS の名前で引くのは台本を読み終えてから */
            char *n;
            sp++;
            n = exprname();
            o->phname = xrealloc(o->phname, (long)(o->nph + 1) * (long)sizeof(char *));
            o->phname[o->nph++] = n;
        } else if (*sp == '=' && sp[1] != '=') {
            sp++;
            parse_expr();
        } else
            break;
    }
    accept(',');
    return s;
}

static void parse_script(const char *text, const char *name)
{
    struct stmt **tail = &top;
    sp = text;
    scriptname = name;
    while (*tail)
        tail = &(*tail)->next;
    for (;;) {
        int c = peekc();
        if (!c)
            break;
        if (c == ';') {
            sp++;
            continue;
        }
        if (keyword("ENTRY")) {
            expect('(');
            entryname = exprname();
            expect(')');
            continue;
        }
        if (keyword("EXTERN")) {
            /* 未定義の参照として加える (lib-ksyms.o の台本。書庫のメンバを引き込む) */
            expect('(');
            while (!accept(')')) {
                gsym(exprname(), 1)->referenced = 1;
                accept(',');
            }
            continue;
        }
        if (keyword("OUTPUT_ARCH") || keyword("OUTPUT_FORMAT") || keyword("TARGET")
            || keyword("SEARCH_DIR") || keyword("INPUT") || keyword("GROUP")
            || keyword("STARTUP") || keyword("OUTPUT")) {
            expect('(');
            while (!accept(')')) {
                char *n = patname();
                (void)n;
                accept(',');
            }
            continue;
        }
        if (keyword("PROVIDE")) { append(&tail, provide(1, 0)); continue; }
        if (keyword("PROVIDE_HIDDEN")) { append(&tail, provide(1, 1)); continue; }
        if (keyword("HIDDEN")) { append(&tail, provide(0, 1)); continue; }
        if (keyword("ASSERT")) {
            struct stmt *s = mkstmt(S_ASSERT);
            expect('(');
            s->e = parse_expr();
            expect(',');
            s->msg = exprname();
            expect(')');
            append(&tail, s);
            continue;
        }
        if (keyword("PHDRS")) {
            struct phdr **pt = &phdrs;
            while (*pt)
                pt = &(*pt)->next;
            expect('{');
            while (!accept('}')) {
                struct phdr *p = xmalloc(sizeof(struct phdr));
                char *t;
                p->name = exprname();
                t = exprname();
                if (strcmp(t, "PT_NULL") == 0)
                    p->type = 0;
                else if (strcmp(t, "PT_LOAD") == 0)
                    p->type = PT_LOAD;
                else if (strcmp(t, "PT_DYNAMIC") == 0)
                    p->type = PT_DYNAMIC;
                else if (strcmp(t, "PT_NOTE") == 0)
                    p->type = PT_NOTE;
                else if (strcmp(t, "PT_PHDR") == 0)
                    p->type = PT_PHDR;
                else if (strcmp(t, "PT_GNU_EH_FRAME") == 0)
                    p->type = PT_GNU_EH_FRAME;
                else if (strcmp(t, "PT_GNU_STACK") == 0)
                    p->type = PT_GNU_STACK;
                else
                    serr("知らない program header の型");
                for (;;) {
                    if (keyword("FILEHDR"))
                        p->filehdr = 1;
                    else if (keyword("PHDRS"))
                        p->phdrs = 1;
                    else if (keyword("FLAGS")) {
                        expect('(');
                        p->flagexpr = parse_expr();
                        expect(')');
                    } else
                        break;
                }
                expect(';');
                *pt = p;
                pt = &p->next;
                nphdrs++;
            }
            continue;
        }
        if (keyword("VERSION")) {
            /* 版の節点を 1 つだけ持つ: name { global: a; b; local: *; } ; */
            expect('{');
            while (!accept('}')) {
                char *node = exprname();
                int glob = 1;
                if (vername)
                    serr("VERSION の節点は 1 つだけ持つ");
                vername = node;
                expect('{');
                while (!accept('}')) {
                    char *n;
                    if (keyword("global")) {
                        expect(':');
                        glob = 1;
                        continue;
                    }
                    if (keyword("local")) {
                        expect(':');
                        glob = 0;
                        continue;
                    }
                    n = patname();
                    expect(';');
                    if (!glob) {
                        if (strcmp(n, "*") != 0)
                            serr("local: は * だけを持つ");
                        continue;
                    }
                    verglobal = xrealloc(verglobal, (long)(nverglobal + 1) * (long)sizeof(char *));
                    verglobal[nverglobal++] = n;
                }
                if (peekc() != ';')
                    exprname();
                expect(';');
            }
            continue;
        }
        if (keyword("SECTIONS")) {
            expect('{');
            while (!accept('}')) {
                if (accept(';'))
                    continue;
                if (keyword("ENTRY")) {
                    expect('(');
                    entryname = exprname();
                    expect(')');
                    continue;
                }
                if (keyword("PROVIDE")) { append(&tail, provide(1, 0)); continue; }
                if (keyword("PROVIDE_HIDDEN")) { append(&tail, provide(1, 1)); continue; }
                if (keyword("HIDDEN")) { append(&tail, provide(0, 1)); continue; }
                if (keyword("ASSERT")) {
                    struct stmt *s = mkstmt(S_ASSERT);
                    expect('(');
                    s->e = parse_expr();
                    expect(',');
                    s->msg = exprname();
                    expect(')');
                    append(&tail, s);
                    continue;
                }
                {
                    const char *save = sp;
                    char *n;
                    skipws();
                    if (*sp == '.' && !isnamec(sp[1])) {
                        sp++;
                        append(&tail, assignment(xstrdup(".")));
                        accept(';');
                        continue;
                    }
                    /* 名前の後ろが代入の演算子 (= += -= *= /= %= &= |= ^= <<= >>=)
                     * なら記号への代入，そうでなければ出力節 */
                    if (isnamec(*sp)) {
                        const char *q = sp;
                        while (isnamec(*q))
                            q++;
                        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                            q++;
                        if ((q[0] == '=' && q[1] != '=')
                            || (q[1] == '=' && q[0] && strchr("+-*/%&|^", q[0]))
                            || ((q[0] == '<' || q[0] == '>') && q[1] == q[0] && q[2] == '=')) {
                            n = exprname();
                            append(&tail, assignment(n));
                            accept(';');
                            continue;
                        }
                    }
                    sp = save;
                    n = patname();
                    append(&tail, output_section(n));
                }
            }
            continue;
        }
        {
            char *n = exprname();
            append(&tail, assignment(n));
            accept(';');
        }
    }
}

/* ---- 照合 ---- */
static int wildmatch(const char *p, const char *s)
{
    while (*p) {
        if (*p == '*') {
            p++;
            if (!*p)
                return 1;
            while (*s) {
                if (wildmatch(p, s))
                    return 1;
                s++;
            }
            return 0;
        }
        if (*p == '?') {
            if (!*s)
                return 0;
        } else if (*p == '[') {
            int ok = 0, neg = 0;
            p++;
            if (*p == '!' || *p == '^') {
                neg = 1;
                p++;
            }
            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    if (*s >= p[0] && *s <= p[2])
                        ok = 1;
                    p += 3;
                } else {
                    if (*s == *p)
                        ok = 1;
                    p++;
                }
            }
            if (ok == neg || !*s)
                return 0;
        } else if (*p != *s)
            return 0;
        p++;
        s++;
    }
    return *s == 0;
}

static const char *basename_of(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

static int filematch(struct stmt *s, struct ifile *f)
{
    int i;
    if (strcmp(s->filepat, "*") != 0 && !wildmatch(s->filepat, f->name)
        && !wildmatch(s->filepat, basename_of(f->name)))
        return 0;
    for (i = 0; i < s->nexcl; i++)
        if (wildmatch(s->excl[i], f->name) || wildmatch(s->excl[i], basename_of(f->name)))
            return 0;
    return 1;
}

static int secmatch(struct stmt *s, struct isec *x)
{
    int i;
    for (i = 0; i < s->nsecpat; i++)
        if (wildmatch(s->secpat[i], x->name))
            return 1;
    return 0;
}

static void addlist(struct stmt *s, struct isec *x)
{
    s->list = xrealloc(s->list, (long)(s->nlist + 1) * (long)sizeof(struct isec *));
    s->list[s->nlist++] = x;
}

static void sortlist(struct stmt *s)
{
    int i, j;
    for (i = 1; i < s->nlist; i++) {
        struct isec *t = s->list[i];
        for (j = i - 1; j >= 0 && strcmp(s->list[j]->name, t->name) > 0; j--)
            s->list[j + 1] = s->list[j];
        s->list[j + 1] = t;
    }
}

/* 入力節の指定を台本の順に見て，最初に合ったものに節を割り当てる */
static void assign_spec(struct stmt *s, struct osec *o)
{
    int i, k;
    struct ifile *f;
    for (i = 0; i <= nfiles; i++) {
        f = i < nfiles ? files[i] : commonfile;
        if (!f || !filematch(s, f))
            continue;
        for (k = 0; k < f->nsec; k++) {
            struct isec *x = f->secs[k];
            if (!x || x->assigned || !secmatch(s, x))
                continue;
            x->assigned = 1;
            if (o->discard) {
                x->discard = 1;
                continue;
            }
            x->out = o;
            addlist(s, x);
        }
    }
    if (s->sort)
        sortlist(s);
}

static int kindof(u32 flags, int type)
{
    if (flags & SHF_EXECINSTR)
        return 0;
    if (type == SHT_NOBITS)
        return 3;
    if (flags & SHF_WRITE)
        return 2;
    return 1;
}

/* 台本が置かなかった節を，同じ種類の最後の出力節の後ろへ置く */
static void place_orphans(void)
{
    int i, k;
    struct stmt *s;
    for (s = top; s; s = s->next) {
        struct stmt *b;
        if (s->kind != S_OSEC || s->os->discard)
            continue;
        for (b = s->os->body; b; b = b->next) {
            int j;
            if (b->kind != S_INPUT)
                continue;
            for (j = 0; j < b->nlist; j++) {
                struct isec *x = b->list[j];
                int kd = kindof(x->flags, x->type);
                if (s->os->kind < 0 || kd < s->os->kind)
                    s->os->kind = kd;
            }
        }
    }
    for (i = 0; i <= nfiles; i++) {
        struct ifile *f = i < nfiles ? files[i] : commonfile;
        for (k = 0; f && k < f->nsec; k++) {
            struct isec *x = f->secs[k];
            struct stmt *last = 0, *t, *in;
            struct osec *o = 0;
            int kd;
            if (!x || x->assigned)
                continue;
            kd = kindof(x->flags, x->type);
            /* 同じ名前の orphan の出力節が既にあればそこへ */
            for (t = top; t; t = t->next)
                if (t->kind == S_OSEC && t->os->orphan && strcmp(t->os->name, x->name) == 0)
                    o = t->os;
            if (!o) {
                for (t = top; t; t = t->next)
                    if (t->kind == S_OSEC && !t->os->discard && t->os->kind == kd)
                        last = t;
                o = new_osec(x->name);
                o->orphan = 1;
                o->kind = kd;
                in = mkstmt(S_INPUT);
                in->filepat = "*";
                o->body = in;
                t = mkstmt(S_OSEC);
                t->os = o;
                if (last) {
                    t->next = last->next;
                    last->next = t;
                } else {
                    struct stmt **pp = &top;
                    while (*pp)
                        pp = &(*pp)->next;
                    *pp = t;
                }
            }
            x->assigned = 1;
            x->out = o;
            addlist(o->body, x);
        }
    }
}

static void assign_all(void)
{
    struct stmt *s, *b;
    for (s = top; s; s = s->next) {
        if (s->kind != S_OSEC)
            continue;
        for (b = s->os->body; b; b = b->next)
            if (b->kind == S_INPUT)
                assign_spec(b, s->os);
    }
    place_orphans();
}

/* ---- 式の評価 ---- */
/* 値と，それが番地 (節に結び付く) か数か */
struct ev {
    long long v;
    int addr;
};

static struct osec *findosec(const char *name)
{
    struct stmt *s;
    for (s = top; s; s = s->next)
        if (s->kind == S_OSEC && strcmp(s->os->name, name) == 0)
            return s->os;
    return 0;
}

static u32 symaddr(struct gsym *g)
{
    if (g->sec)
        return (g->sec->addr + g->value) & 0xffffffffUL;
    return g->value;
}

static struct ev eval(struct expr *e)
{
    struct ev r, a, b;
    r.v = 0;
    r.addr = 0;
    switch (e->kind) {
    case E_NUM:
        r.v = e->num;
        return r;
    case E_DOT:
        r.v = dot;
        r.addr = 1;
        return r;
    case E_SYM: {
        struct gsym *g = gsym(e->name, 0);
        if (!g || !g->defined) {
            if (final)
                fatal("台本が未定義の記号を参照した", e->name);
            return r;
        }
        r.v = symaddr(g);
        r.addr = g->sec != 0 || g->osec != 0;
        return r;
    }
    case E_UN:
        a = eval(e->a);
        r.v = e->op == '-' ? -a.v : e->op == '~' ? ~a.v : !a.v;
        return r;
    case E_COND:
        a = eval(e->a);
        return a.v ? eval(e->b) : eval(e->c);
    case E_BIN:
        a = eval(e->a);
        b = eval(e->b);
        r.addr = (e->op == '+' && (a.addr || b.addr)) || (e->op == '-' && a.addr && !b.addr);
        switch (e->op) {
        case '+': r.v = a.v + b.v; break;
        case '-': r.v = a.v - b.v; break;
        case '*': r.v = a.v * b.v; break;
        case '/': if (!b.v) fatal("台本で 0 で割った", 0); r.v = a.v / b.v; break;
        case '%': if (!b.v) fatal("台本で 0 で割った", 0); r.v = a.v % b.v; break;
        case 'L': r.v = a.v << b.v; break;
        case 'R': r.v = (long long)((unsigned long long)a.v >> b.v); break;
        case '&': r.v = a.v & b.v; break;
        case '|': r.v = a.v | b.v; break;
        case '^': r.v = a.v ^ b.v; break;
        case 'E': r.v = a.v == b.v; break;
        case 'N': r.v = a.v != b.v; break;
        case '<': r.v = a.v < b.v; break;
        case '>': r.v = a.v > b.v; break;
        case 'l': r.v = a.v <= b.v; break;
        case 'g': r.v = a.v >= b.v; break;
        case 'A': r.v = a.v && b.v; break;
        case 'O': r.v = a.v || b.v; break;
        }
        return r;
    case E_FUNC: {
        const char *f = e->name;
        if (strcmp(f, "ALIGN") == 0 || strcmp(f, "NEXT") == 0) {
            if (e->b) {
                a = eval(e->a);
                b = eval(e->b);
                r.v = (long long)alignup((u32)a.v, (u32)b.v);
                r.addr = a.addr;
            } else {
                a = eval(e->a);
                r.v = (long long)alignup(dot, (u32)a.v);
                r.addr = 1;
            }
            return r;
        }
        if (strcmp(f, "ABSOLUTE") == 0) {
            r = eval(e->a);
            r.addr = 0;
            return r;
        }
        if (strcmp(f, "MAX") == 0 || strcmp(f, "MIN") == 0) {
            a = eval(e->a);
            b = eval(e->b);
            r = (f[1] == 'A') == (a.v > b.v) ? a : b;
            return r;
        }
        if (strcmp(f, "LOG2CEIL") == 0) {
            long long v = eval(e->a).v, k = 0;
            while ((1LL << k) < v)
                k++;
            r.v = k;
            return r;
        }
        if (strcmp(f, "SIZEOF_HEADERS") == 0) {
            /* ELF 見出しと program header の大きさ (PHDRS のあるときだけ正しい) */
            r.v = 52 + 32 * nphdrs;
            return r;
        }
        if (strcmp(f, "CONSTANT") == 0) {
            /* MAXPAGESIZE / COMMONPAGESIZE */
            r.v = 0x1000;
            return r;
        }
        if (strcmp(f, "DEFINED") == 0) {
            struct gsym *g = gsym(e->a->name, 0);
            r.v = g && g->defined;
            return r;
        }
        {
            struct osec *o = findosec(e->a->name);
            if (!o) {
                if (final)
                    fatal("台本が知らない出力節を参照した", e->a->name);
                return r;
            }
            if (strcmp(f, "ADDR") == 0) {
                r.v = o->vma;
                r.addr = 1;
            } else if (strcmp(f, "LOADADDR") == 0)
                r.v = o->lma;
            else if (strcmp(f, "SIZEOF") == 0)
                r.v = o->size;
            else if (strcmp(f, "ALIGNOF") == 0)
                r.v = o->maxalign;
            return r;
        }
    }
    }
    return r;
}

static void setsym(struct stmt *s, struct ev v)
{
    struct gsym *g = gsym(s->sym, 1);
    if (s->provide) {
        /* PROVIDE は参照されていて入力が定義していないときだけ定義する */
        if (!g->referenced || g->defined == 1)
            return;
    }
    /* 入力が定義した記号への代入は台本が勝つ (GNU ld と同じ。vmlinux.lds の
     * `_start = .;` と head.S の _start) */
    g->defined = 2;
    g->sec = 0;
    g->value = (u32)v.v & 0xffffffffUL;
    g->osec = v.addr ? cursec : 0;
    g->hidden = s->hidden;
}

/* ---- 割付け ---- */
static void layout_body(struct osec *o)
{
    struct stmt *b;
    for (b = o->body; b; b = b->next) {
        if (b->kind == S_ASSIGN) {
            struct ev v = eval(b->e);
            if (strcmp(b->sym, ".") == 0) {
                /* 出力節の中で数を代入すると節の先頭からの位置になる */
                u32 nd = v.addr ? (u32)v.v : o->vma + (u32)v.v;
                if (nd < dot && final)
                    fatal("出力節の中で . を戻した", o->name);
                dot = nd & 0xffffffffUL;
            } else
                setsym(b, v);
        } else if (b->kind == S_INPUT) {
            int i;
            for (i = 0; i < b->nlist; i++) {
                struct isec *x = b->list[i];
                dot = alignup(dot, x->align);
                if (x->align > o->maxalign)
                    o->maxalign = x->align;
                x->addr = dot;
                dot = (dot + x->size) & 0xffffffffUL;
            }
        } else if (b->kind == S_DATA)
            dot += (u32)b->dsize;
        else if (b->kind == S_ASSERT && final) {
            if (!eval(b->e).v)
                fatal("ASSERT", b->msg);
        }
    }
}

/* 出力節の旗と大きさの上限の整列を，割り付ける前に入力から求める */
static void osec_attrs(struct osec *o)
{
    struct stmt *b;
    int any = 0, nobits = 1, same = -1;
    o->flags = SHF_ALLOC;
    for (b = o->body; b; b = b->next) {
        int i;
        if (b->kind == S_DATA)
            nobits = 0;
        if (b->kind != S_INPUT)
            continue;
        for (i = 0; i < b->nlist; i++) {
            struct isec *x = b->list[i];
            any = 1;
            o->flags |= x->flags & (SHF_WRITE | SHF_EXECINSTR);
            if (x->type != SHT_NOBITS)
                nobits = 0;
            if (x->align > o->maxalign)
                o->maxalign = x->align;
            /* 入力の型がすべて同じなら (.note・動的な節) その型にする */
            same = same == -1 || same == x->type ? x->type : -2;
        }
    }
    o->type = (nobits && any) || o->noload ? SHT_NOBITS : same >= 0 ? same : SHT_PROGBITS;
}

static void layout(void)
{
    struct stmt *s;
    u32 lmaoff = 0;
    int haslma = 0;
    dot = 0;
    cursec = 0;
    for (s = top; s; s = s->next) {
        if (s->kind == S_ASSIGN) {
            struct ev v = eval(s->e);
            if (strcmp(s->sym, ".") == 0)
                dot = (u32)v.v & 0xffffffffUL;
            else
                setsym(s, v);
        } else if (s->kind == S_ASSERT) {
            if (final && !eval(s->e).v)
                fatal("ASSERT", s->msg);
        } else if (s->kind == S_OSEC) {
            struct osec *o = s->os;
            if (o->discard)
                continue;
            if (o->addr)
                dot = (u32)eval(o->addr).v & 0xffffffffUL;
            else
                dot = alignup(dot, o->maxalign);
            if (o->align)
                dot = alignup(dot, (u32)eval(o->align).v);
            o->vma = dot;
            if (o->at) {
                o->lma = (u32)eval(o->at).v & 0xffffffffUL;
                lmaoff = o->lma - o->vma;
                haslma = 1;
            } else
                o->lma = haslma ? (o->vma + lmaoff) & 0xffffffffUL : o->vma;
            cursec = o;
            layout_body(o);
            cursec = 0;
            o->size = (dot - o->vma) & 0xffffffffUL;
        }
    }
}

/* 割付けの結果 (出力節の番地・大きさと台本の記号の値) の要約。
 * 回ごとの値の変化を見るのに使う */
static u32 layout_digest(void)
{
    u32 h = 2166136261UL;
    struct stmt *s;
    int k;
    struct gsym *g;
    for (s = top; s; s = s->next)
        if (s->kind == S_OSEC) {
            h = ((h ^ s->os->vma) * 16777619UL) & 0xffffffffUL;
            h = ((h ^ s->os->lma) * 16777619UL) & 0xffffffffUL;
            h = ((h ^ s->os->size) * 16777619UL) & 0xffffffffUL;
        }
    for (k = 0; k < 4096; k++)
        for (g = gtab[k]; g; g = g->next)
            if (g->defined == 2)
                h = ((h ^ g->value) * 16777619UL) & 0xffffffffUL;
    return h;
}

/* ---- 再配置 ---- */
struct hi {
    struct isec *sec;
    u32 off;
    u32 value;
};

static struct hi *his;
static int nhi;
static int nundef;              /* 未定義の参照の数 */

/* 入力の記号表の k 番の番地 (局所なら節 + 値) */
static int symvalue(struct isec *x, int k, u32 *out, const char **name)
{
    struct ifile *f = x->file;
    struct lsym *ls;
    if (k <= 0 || k >= f->nsym)
        fatal("再配置の記号の番号が範囲外", f->name);
    ls = &f->syms[k];
    *name = ls->name;
    /* 節の記号は名前を持たないので，診断には節の名前を出す */
    if (ls->type == STT_SECTION && ls->shndx < f->nsec && f->secs[ls->shndx])
        *name = f->secs[ls->shndx]->name;
    if (ls->g) {
        struct gsym *g = ls->g;
        if (!g->defined) {
            if (ls->bind == STB_WEAK || g->weak) {
                *out = 0;
                return 1;
            }
            return 0;
        }
        if (g->sec && g->sec->discard)
            fatal("捨てた節の記号を参照した", g->name);
        *out = symaddr(g);
        return 1;
    }
    if (ls->shndx == SHN_ABS) {
        *out = ls->value;
        return 1;
    }
    if (ls->shndx >= f->nsec || !f->secs[ls->shndx]) {
        if (ls->shndx == SHN_UNDEF)
            return 0;
        fatal("割り付けない節の記号を参照した", ls->name);
    }
    if (f->secs[ls->shndx]->discard)
        fatal("捨てた節を参照した", f->secs[ls->shndx]->name);
    *out = (f->secs[ls->shndx]->addr + (ls->type == STT_SECTION ? 0 : ls->value)) & 0xffffffffUL;
    return 1;
}

static void relerr(struct isec *x, u32 off, const char *msg, const char *sym)
{
    fprintf(stderr, "nld1: %s(%s+0x%lx): %s: %s\n", x->file->name, x->name,
            (unsigned long)off, msg, sym ? sym : "");
    exit(1);
}

static long sext(u32 v, int bits)
{
    u32 m = 1UL << (bits - 1);
    v &= (m << 1) - 1;
    return (long)(v ^ m) - (long)m;
}

static void put_i(unsigned char *p, u32 imm)
{
    u32 w = rd32(p);
    wr32(p, (w & 0x000fffffUL) | ((imm & 0xfff) << 20));
}

static void put_s(unsigned char *p, u32 imm)
{
    u32 w = rd32(p);
    w &= 0x01fff07fUL;
    w |= ((imm >> 5) & 0x7f) << 25;
    w |= (imm & 0x1f) << 7;
    wr32(p, w);
}

static void put_u(unsigned char *p, u32 hi20)
{
    u32 w = rd32(p);
    wr32(p, (w & 0xfffUL) | ((hi20 & 0xfffff) << 12));
}

static void put_b(unsigned char *p, u32 v)
{
    u32 w = rd32(p) & 0x01fff07fUL;
    w |= ((v >> 12) & 1) << 31;
    w |= ((v >> 5) & 0x3f) << 25;
    w |= ((v >> 1) & 0xf) << 8;
    w |= ((v >> 11) & 1) << 7;
    wr32(p, w);
}

static void put_j(unsigned char *p, u32 v)
{
    u32 w = rd32(p) & 0xfffUL;
    w |= ((v >> 20) & 1) << 31;
    w |= ((v >> 1) & 0x3ff) << 21;
    w |= ((v >> 11) & 1) << 20;
    w |= ((v >> 12) & 0xff) << 12;
    wr32(p, w);
}

static u32 hi20(u32 v)
{
    return ((v + 0x800) >> 12) & 0xfffff;
}

static u32 lo12(u32 v)
{
    return (v - (hi20(v) << 12)) & 0xfff;
}

static void addn(unsigned char *p, int n, long long v, int sub)
{
    unsigned long long cur = 0;
    int i;
    for (i = n - 1; i >= 0; i--)
        cur = (cur << 8) | p[i];
    cur = sub ? cur - (unsigned long long)v : cur + (unsigned long long)v;
    for (i = 0; i < n; i++) {
        p[i] = (unsigned char)(cur & 0xff);
        cur >>= 8;
    }
}

static void collect_hi(struct isec *x)
{
    int i;
    for (i = 0; i < x->nrel; i++) {
        unsigned char *r = x->rel + i * 12;
        u32 off = rd32(r), info = rd32(r + 4), s;
        long addend = (long)sext(rd32(r + 8), 32);
        const char *name;
        if ((info & 0xff) != R_RISCV_PCREL_HI20)
            continue;
        if (!symvalue(x, (int)(info >> 8), &s, &name))
            relerr(x, off, "未定義の記号", name);
        his = xrealloc(his, (long)(nhi + 1) * (long)sizeof(struct hi));
        his[nhi].sec = x;
        his[nhi].off = off;
        his[nhi].value = (s + (u32)addend - (x->addr + off)) & 0xffffffffUL;
        nhi++;
    }
}

static void relocate(struct isec *x, unsigned char *base)
{
    int i;
    for (i = 0; i < x->nrel; i++) {
        unsigned char *r = x->rel + i * 12;
        u32 off = rd32(r), info = rd32(r + 4), s = 0;
        int type = (int)(info & 0xff);
        long addend = (long)sext(rd32(r + 8), 32);
        u32 p = (x->addr + off) & 0xffffffffUL, v;
        unsigned char *loc = base + off;
        const char *name = "";
        if (type == R_RISCV_NONE || type == R_RISCV_RELAX)
            continue;
        if (off + 4 > x->size && type != R_RISCV_ADD8 && type != R_RISCV_SUB8
            && type != R_RISCV_ADD16 && type != R_RISCV_SUB16)
            relerr(x, off, "再配置が節の外", 0);
        if (!symvalue(x, (int)(info >> 8), &s, &name)) {
            /* 未定義の参照はすべて出してから止める (fill_sections の後) */
            fprintf(stderr, "nld1: %s(%s+0x%lx): 未定義の記号: %s\n", x->file->name, x->name,
                    (unsigned long)off, name);
            nundef++;
            continue;
        }
        v = (s + (u32)addend) & 0xffffffffUL;
        switch (type) {
        case R_RISCV_32:
            wr32(loc, v);
            break;
        case R_RISCV_64: {
            long long full = (long long)s + addend;
            wr32(loc, (u32)(full & 0xffffffffLL));
            wr32(loc + 4, (u32)((full >> 32) & 0xffffffffLL));
            break;
        }
        case R_RISCV_BRANCH: {
            long d = sext((v - p) & 0xffffffffUL, 32);
            if (d < -4096 || d > 4094 || (d & 1))
                relerr(x, off, "分岐の行き先が届かない", name);
            put_b(loc, (u32)d);
            break;
        }
        case R_RISCV_JAL: {
            long d = sext((v - p) & 0xffffffffUL, 32);
            if (d < -1048576 || d > 1048574 || (d & 1))
                relerr(x, off, "jal の行き先が届かない", name);
            put_j(loc, (u32)d);
            break;
        }
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT: {
            u32 d = (v - p) & 0xffffffffUL;
            if (off + 8 > x->size)
                relerr(x, off, "call の対が節の外", name);
            put_u(loc, hi20(d));
            put_i(loc + 4, lo12(d));
            break;
        }
        case R_RISCV_PCREL_HI20:
            put_u(loc, hi20((v - p) & 0xffffffffUL));
            break;
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S: {
            /* 記号は auipc の位置のラベル。その位置の HI20 の値を使う */
            int k;
            u32 at = s;
            for (k = 0; k < nhi; k++)
                if (((his[k].sec->addr + his[k].off) & 0xffffffffUL) == at)
                    break;
            if (k == nhi)
                relerr(x, off, "%pcrel_lo に対応する %pcrel_hi が無い", name);
            if (type == R_RISCV_PCREL_LO12_I)
                put_i(loc, lo12(his[k].value));
            else
                put_s(loc, lo12(his[k].value));
            break;
        }
        case R_RISCV_HI20:
            put_u(loc, hi20(v));
            break;
        case R_RISCV_LO12_I:
            put_i(loc, lo12(v));
            break;
        case R_RISCV_LO12_S:
            put_s(loc, lo12(v));
            break;
        case R_RISCV_ADD8: addn(loc, 1, (long long)v, 0); break;
        case R_RISCV_ADD16: addn(loc, 2, (long long)v, 0); break;
        case R_RISCV_ADD32: addn(loc, 4, (long long)v, 0); break;
        case R_RISCV_ADD64: addn(loc, 8, (long long)v, 0); break;
        case R_RISCV_SUB8: addn(loc, 1, (long long)v, 1); break;
        case R_RISCV_SUB16: addn(loc, 2, (long long)v, 1); break;
        case R_RISCV_SUB32: addn(loc, 4, (long long)v, 1); break;
        case R_RISCV_SUB64: addn(loc, 8, (long long)v, 1); break;
        case R_RISCV_ALIGN:
            relerr(x, off, "R_RISCV_ALIGN は扱わない (緩和をしない)", 0);
            break;
        default: {
            char m[48];
            sprintf(m, "扱わない再配置の型 %d", type);
            relerr(x, off, m, name);
        }
        }
    }
}

/* 出力節の中身を作り，再配置を当てる */
static void fill_sections(void)
{
    struct stmt *s, *b;
    for (s = top; s; s = s->next) {
        struct osec *o;
        if (s->kind != S_OSEC || s->os->discard)
            continue;
        o = s->os;
        if (o->type == SHT_NOBITS || o->size == 0)
            continue;
        o->data = xmalloc((long)o->size);
        dot = o->vma;
        for (b = o->body; b; b = b->next) {
            int i;
            if (b->kind == S_INPUT) {
                for (i = 0; i < b->nlist; i++) {
                    struct isec *x = b->list[i];
                    if (x->data)
                        memcpy(o->data + (x->addr - o->vma), x->data, x->size);
                    dot = x->addr + x->size;
                }
            } else if (b->kind == S_DATA) {
                long long v = eval(b->e).v;
                u32 at = dot - o->vma;
                for (i = 0; i < b->dsize; i++) {
                    o->data[at + i] = (unsigned char)(v & 0xff);
                    v >>= 8;
                }
                dot += (u32)b->dsize;
            } else if (b->kind == S_ASSIGN && strcmp(b->sym, ".") == 0) {
                struct ev v = eval(b->e);
                dot = v.addr ? (u32)v.v : o->vma + (u32)v.v;
            }
        }
    }
    for (s = top; s; s = s->next)
        if (s->kind == S_OSEC && !s->os->discard)
            for (b = s->os->body; b; b = b->next)
                if (b->kind == S_INPUT) {
                    int i;
                    for (i = 0; i < b->nlist; i++)
                        collect_hi(b->list[i]);
                }
    for (s = top; s; s = s->next) {
        struct osec *o;
        if (s->kind != S_OSEC || s->os->discard)
            continue;
        o = s->os;
        for (b = o->body; b; b = b->next)
            if (b->kind == S_INPUT) {
                int i;
                for (i = 0; i < b->nlist; i++) {
                    struct isec *x = b->list[i];
                    if (!x->nrel)
                        continue;
                    if (!o->data)
                        relerr(x, 0, "nobits の節に再配置がある", 0);
                    relocate(x, o->data + (x->addr - o->vma));
                }
            }
    }
}

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

static int osec_index(struct osec *o);

/* ---- リンカが作る節 ----
 * -shared は .hash・.gnu.hash・.dynsym・.dynstr・.gnu.version・
 * .gnu.version_d・.dynamic を，--build-id は .note.gnu.build-id を作る。
 * 擬似ファイル "(nld1)" の入力節として台本に割り当てる。大きさは割付けの
 * 前に決め，中身は割付けの後 (番地が決まってから) 書く。
 *
 * 動的な記号は，VERSION があればその global: の記号，無ければ定義された
 * 大域の記号 (hidden を除く) である。VERSION の節点の名前も絶対値の記号と
 * して加える (GNU ld と同じ)。動的な再配置は作らない (vdso の .S は位置に
 * 依らない) */
#define SHT_HASH 5
#define SHT_DYNSYM 11
#define SHT_GNU_HASH 0x6ffffff6UL
#define SHT_GNU_VERDEF 0x6ffffffdUL
#define SHT_GNU_VERSYM 0x6fffffffUL
#define SHT_DYNAMIC 6
#define SHT_NOTE 7

static struct ifile *synfile;
static struct isec *s_hash, *s_gnuhash, *s_dynsym, *s_dynstr, *s_versym, *s_verdef,
    *s_dynamic, *s_buildid;

struct dsym {
    const char *name;
    struct gsym *g;             /* 0 なら版の節点の記号 */
    u32 hash;                   /* GNU の hash */
    long nameoff;
};

static struct dsym *dsyms;      /* [0] は空 */
static int ndsyms;
static struct strtab dynstr;
static u32 gnb;                 /* .gnu.hash の bucket の数 */

static struct isec *synsec(const char *name, u32 type, u32 flags, u32 align, u32 size)
{
    struct isec *x = xmalloc(sizeof(struct isec));
    if (!synfile) {
        synfile = xmalloc(sizeof(struct ifile));
        synfile->name = "(nld1)";
        files = xrealloc(files, (long)(nfiles + 1) * (long)sizeof(struct ifile *));
        files[nfiles++] = synfile;
    }
    synfile->secs = xrealloc(synfile->secs, (long)(synfile->nsec + 1) * (long)sizeof(struct isec *));
    x->file = synfile;
    x->idx = synfile->nsec;
    synfile->secs[synfile->nsec++] = x;
    x->name = (char *)name;
    x->type = (int)type;
    x->flags = flags;
    x->align = align;
    x->size = size;
    x->data = xmalloc((long)size);
    return x;
}

static u32 elfhash(const char *s)
{
    u32 h = 0, g;
    while (*s) {
        h = ((h << 4) + (unsigned char)*s++) & 0xffffffffUL;
        g = h & 0xf0000000UL;
        if (g)
            h ^= g >> 24;
        h &= ~g & 0xffffffffUL;
    }
    return h;
}

static u32 gnuhash(const char *s)
{
    u32 h = 5381;
    while (*s)
        h = (h * 33 + (unsigned char)*s++) & 0xffffffffUL;
    return h;
}

static int exported(struct gsym *g)
{
    int i;
    if (!g->defined || g->hidden)
        return 0;
    if (!vername)
        return 1;
    for (i = 0; i < nverglobal; i++)
        if (strcmp(verglobal[i], g->name) == 0)
            return 1;
    return 0;
}

static int dsym_cmp(const void *a, const void *b)
{
    const struct dsym *x = a, *y = b;
    if (x->hash % gnb != y->hash % gnb)
        return x->hash % gnb < y->hash % gnb ? -1 : 1;
    return strcmp(x->name, y->name);
}

/* 動的な節を作る (大きさだけ) */
static void make_dynamic(void)
{
    int h;
    struct gsym *g;
    u32 n;
    ndsyms = 1;
    dsyms = xmalloc(sizeof(struct dsym));
    for (h = 0; h < 4096; h++)
        for (g = gtab[h]; g; g = g->next)
            if (exported(g)) {
                dsyms = xrealloc(dsyms, (long)(ndsyms + 1) * (long)sizeof(struct dsym));
                dsyms[ndsyms].name = g->name;
                dsyms[ndsyms].g = g;
                ndsyms++;
            }
    if (vername) {
        dsyms = xrealloc(dsyms, (long)(ndsyms + 1) * (long)sizeof(struct dsym));
        dsyms[ndsyms].name = vername;
        dsyms[ndsyms].g = 0;
        ndsyms++;
    }
    n = (u32)ndsyms;
    gnb = n > 2 ? (n - 1) / 2 : 1;
    for (h = 1; h < ndsyms; h++)
        dsyms[h].hash = gnuhash(dsyms[h].name);
    /* .gnu.hash は bucket の順に並べた記号を要る */
    qsort(dsyms + 1, (size_t)(ndsyms - 1), sizeof(struct dsym), dsym_cmp);
    stradd(&dynstr, "");
    if (!soname)
        soname = "";
    if (soname[0])
        stradd(&dynstr, soname);
    for (h = 1; h < ndsyms; h++)
        dsyms[h].nameoff = stradd(&dynstr, dsyms[h].name);
    s_hash = synsec(".hash", SHT_HASH, SHF_ALLOC, 4, 4 * (2 + n + n));
    s_gnuhash = synsec(".gnu.hash", SHT_GNU_HASH, SHF_ALLOC, 4, 16 + 4 + 4 * gnb + 4 * (n - 1));
    s_dynsym = synsec(".dynsym", SHT_DYNSYM, SHF_ALLOC, 4, 16 * n);
    s_dynstr = synsec(".dynstr", SHT_STRTAB, SHF_ALLOC, 1, (u32)dynstr.size);
    s_versym = synsec(".gnu.version", SHT_GNU_VERSYM, SHF_ALLOC, 2, 2 * n);
    if (vername)
        s_verdef = synsec(".gnu.version_d", SHT_GNU_VERDEF, SHF_ALLOC, 4, 2 * 28);
    s_dynamic = synsec(".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 4, 8 * 12);
}

static u32 secaddr(struct isec *x)
{
    return x ? x->addr : 0;
}

static void put16(unsigned char *p, u32 v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

/* 割付けの後に中身を書く。節の番号 (shndx) が決まっている */
static void fill_dynamic(void)
{
    u32 n = (u32)ndsyms, i;
    unsigned char *p;
    struct osec *strout = s_dynstr->out;
    /* .dynsym */
    for (i = 1; i < n; i++) {
        struct gsym *g = dsyms[i].g;
        p = s_dynsym->data + i * 16;
        wr32(p, (u32)dsyms[i].nameoff);
        if (g) {
            int shndx = g->sec ? (g->sec->out ? g->sec->out->shndx : SHN_ABS) : osec_index(g->osec);
            wr32(p + 4, symaddr(g));
            wr32(p + 8, g->size);
            p[12] = (unsigned char)(((g->weak ? STB_WEAK : STB_GLOBAL) << 4) | (g->type & 0xf));
            put16(p + 14, (u32)shndx);
        } else {
            p[12] = (STB_GLOBAL << 4) | STT_OBJECT;
            put16(p + 14, SHN_ABS);
        }
    }
    /* .dynstr */
    memcpy(s_dynstr->data, dynstr.buf, dynstr.size);
    /* .hash (SysV): nbucket = nchain = n */
    p = s_hash->data;
    wr32(p, n);
    wr32(p + 4, n);
    for (i = 1; i < n; i++) {
        u32 b = elfhash(dsyms[i].name) % n;
        /* 鎖の先頭に差し込む */
        wr32(p + 8 + 4 * n + 4 * i, rd32(p + 8 + 4 * b));
        wr32(p + 8 + 4 * b, i);
    }
    /* .gnu.hash: symoffset 1，bloom 1 語，shift 6 */
    p = s_gnuhash->data;
    wr32(p, gnb);
    wr32(p + 4, 1);
    wr32(p + 8, 1);
    wr32(p + 12, 6);
    {
        u32 bloom = 0;
        for (i = 1; i < n; i++) {
            u32 hv = dsyms[i].hash, b = hv % gnb;
            bloom |= (1UL << (hv % 32)) | (1UL << ((hv >> 6) % 32));
            if (rd32(p + 20 + 4 * b) == 0)
                wr32(p + 20 + 4 * b, i);
            wr32(p + 20 + 4 * gnb + 4 * (i - 1),
                 (hv & ~1UL) | (i + 1 == n || dsyms[i + 1].hash % gnb != b ? 1 : 0));
        }
        wr32(p + 16, bloom & 0xffffffffUL);
    }
    /* .gnu.version: 空は 0 (局所)，他は 2 (VERSION の節点) か 1 (大域) */
    for (i = 1; i < n; i++)
        put16(s_versym->data + 2 * i, vername ? 2 : 1);
    /* .gnu.version_d: 基底 (soname) と節点の 2 つ */
    if (s_verdef) {
        p = s_verdef->data;
        put16(p, 1);
        put16(p + 2, 1);        /* VER_FLG_BASE */
        put16(p + 4, 1);
        put16(p + 6, 1);
        wr32(p + 8, elfhash(soname));
        wr32(p + 12, 20);
        wr32(p + 16, 28);
        wr32(p + 20, soname[0] ? 1 : 0);
        wr32(p + 24, 0);
        p += 28;
        put16(p, 1);
        put16(p + 2, 0);
        put16(p + 4, 2);
        put16(p + 6, 1);
        wr32(p + 8, elfhash(vername));
        wr32(p + 12, 20);
        wr32(p + 16, 0);
        for (i = 1; i < n; i++)
            if (!dsyms[i].g)
                wr32(p + 20, (u32)dsyms[i].nameoff);
        wr32(p + 24, 0);
    }
    /* .dynamic */
    {
        u32 dyn[24];
        int k = 0;
        if (soname[0]) {
            dyn[k++] = 14;      /* DT_SONAME */
            dyn[k++] = 1;
        }
        dyn[k++] = 4; dyn[k++] = secaddr(s_hash);
        dyn[k++] = 0x6ffffef5UL; dyn[k++] = secaddr(s_gnuhash);
        dyn[k++] = 5; dyn[k++] = secaddr(s_dynstr);
        dyn[k++] = 6; dyn[k++] = secaddr(s_dynsym);
        dyn[k++] = 10; dyn[k++] = (u32)dynstr.size;
        dyn[k++] = 11; dyn[k++] = 16;
        dyn[k++] = 0x6ffffff0UL; dyn[k++] = secaddr(s_versym);
        if (s_verdef) {
            dyn[k++] = 0x6ffffffcUL; dyn[k++] = secaddr(s_verdef);
            dyn[k++] = 0x6ffffffdUL; dyn[k++] = 2;
        }
        dyn[k++] = 0; dyn[k++] = 0;
        for (i = 0; i < (u32)k; i++)
            wr32(s_dynamic->data + 4 * i, dyn[i]);
    }
    /* 節見出しの sh_link・sh_info・sh_entsize */
    s_dynsym->out->link = strout;
    s_dynsym->out->info = 1;
    s_dynsym->out->entsize = 16;
    s_hash->out->link = s_dynsym->out;
    s_hash->out->entsize = 4;
    s_gnuhash->out->link = s_dynsym->out;
    s_versym->out->link = s_dynsym->out;
    s_versym->out->entsize = 2;
    if (s_verdef) {
        s_verdef->out->link = strout;
        s_verdef->out->info = 2;
    }
    s_dynamic->out->link = strout;
    s_dynamic->out->entsize = 8;
}

/* --build-id: SHA-1 (FIPS 180-4) を出力のファイル全体で計算する。.note の
 * 中身 (20 バイト) は 0 にしてから計算し，後で書き込む */
static u32 rol(u32 x, int n)
{
    return ((x << n) | (x >> (32 - n))) & 0xffffffffUL;
}

static void sha1_block(u32 *hs, const unsigned char *b)
{
    u32 w[80], a, bb, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((u32)b[4 * i] << 24) | ((u32)b[4 * i + 1] << 16) | ((u32)b[4 * i + 2] << 8)
               | (u32)b[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = hs[0]; bb = hs[1]; c = hs[2]; d = hs[3]; e = hs[4];
    for (i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (bb & c) | ((~bb & 0xffffffffUL) & d);
            k = 0x5a827999UL;
        } else if (i < 40) {
            f = bb ^ c ^ d;
            k = 0x6ed9eba1UL;
        } else if (i < 60) {
            f = (bb & c) | (bb & d) | (c & d);
            k = 0x8f1bbcdcUL;
        } else {
            f = bb ^ c ^ d;
            k = 0xca62c1d6UL;
        }
        t = (rol(a, 5) + f + e + k + w[i]) & 0xffffffffUL;
        e = d;
        d = c;
        c = rol(bb, 30);
        bb = a;
        a = t;
    }
    hs[0] = (hs[0] + a) & 0xffffffffUL;
    hs[1] = (hs[1] + bb) & 0xffffffffUL;
    hs[2] = (hs[2] + c) & 0xffffffffUL;
    hs[3] = (hs[3] + d) & 0xffffffffUL;
    hs[4] = (hs[4] + e) & 0xffffffffUL;
}

static void sha1(const unsigned char *p, long n, unsigned char *out)
{
    u32 hs[5];
    unsigned char last[128];
    long i, rem;
    int k;
    hs[0] = 0x67452301UL;
    hs[1] = 0xefcdab89UL;
    hs[2] = 0x98badcfeUL;
    hs[3] = 0x10325476UL;
    hs[4] = 0xc3d2e1f0UL;
    for (i = 0; i + 64 <= n; i += 64)
        sha1_block(hs, p + i);
    rem = n - i;
    memset(last, 0, sizeof last);
    memcpy(last, p + i, rem);
    last[rem] = 0x80;
    {
        long tot = rem + 1 + 8 > 64 ? 128 : 64;
        unsigned long bits = (unsigned long)n * 8UL;
        /* 長さ (bit) を big endian の 64 bit で。32 bit を超える長さは扱わない */
        last[tot - 4] = (unsigned char)((bits >> 24) & 0xff);
        last[tot - 3] = (unsigned char)((bits >> 16) & 0xff);
        last[tot - 2] = (unsigned char)((bits >> 8) & 0xff);
        last[tot - 1] = (unsigned char)(bits & 0xff);
        last[tot - 5] = (unsigned char)(((unsigned long)n >> 29) & 0xff);
        sha1_block(hs, last);
        if (tot == 128)
            sha1_block(hs, last + 64);
    }
    for (k = 0; k < 5; k++) {
        out[4 * k] = (unsigned char)((hs[k] >> 24) & 0xff);
        out[4 * k + 1] = (unsigned char)((hs[k] >> 16) & 0xff);
        out[4 * k + 2] = (unsigned char)((hs[k] >> 8) & 0xff);
        out[4 * k + 3] = (unsigned char)(hs[k] & 0xff);
    }
}

static void make_buildid(void)
{
    s_buildid = synsec(".note.gnu.build-id", SHT_NOTE, SHF_ALLOC, 4, 36);
    wr32(s_buildid->data, 4);
    wr32(s_buildid->data + 4, 20);
    wr32(s_buildid->data + 8, 3);        /* NT_GNU_BUILD_ID */
    memcpy(s_buildid->data + 12, "GNU", 4);
}

/* ---- 出力 ---- */
static unsigned char *obuf;
static long opos;
static long ocap;

static void out1(int b)
{
    if (opos + 1 > ocap) {
        ocap = ocap ? ocap * 2 : 65536;
        obuf = xrealloc(obuf, ocap);
    }
    obuf[opos++] = (unsigned char)b;
}

static void outn(u32 v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        out1((int)(v & 0xff));
        v >>= 8;
    }
}

static void outpad(long a)
{
    while (opos % a)
        out1(0);
}

static void patch32(long at, u32 v)
{
    wr32(obuf + at, v);
}


static int is_local_name(const char *n)
{
    return n[0] == '.' && n[1] == 'L';
}

/* 出力の記号表の 1 項 */
static void outsym(struct strtab *str, const char *name, u32 value, u32 size, int bind,
                   int type, int other, int shndx)
{
    outn((u32)stradd(str, name), 4);
    outn(value, 4);
    outn(size, 4);
    out1((bind << 4) | (type & 0xf));
    out1(other);
    outn((u32)shndx, 2);
}

/* 台本が定義した記号の節。出力しなかった (空の) 節なら絶対値にする */
static int osec_index(struct osec *o)
{
    return o && o->shndx ? o->shndx : SHN_ABS;
}

static struct osec **olist;     /* 出力する節 (節見出しの 1 番から) */
static int nolist;

/* 出力する節を決めて番号を付ける */
static void number_sections(void)
{
    struct stmt *s;
    for (s = top; s; s = s->next)
        if (s->kind == S_OSEC && !s->os->discard && (s->os->size || s->os->orphan == 0)) {
            struct osec *o = s->os;
            if (o->size == 0 && o->kind < 0)
                continue;
            olist = xrealloc(olist, (long)(nolist + 1) * (long)sizeof(struct osec *));
            olist[nolist++] = o;
            o->shndx = nolist;
        }
}

/* :phdr の名前を PHDRS で引く。指定の無い節は前の節の指定を引き継ぐ
 * (GNU ld と同じ)。:NONE はどの program header にも入れない */
static void resolve_phdrs(void)
{
    struct stmt *s;
    struct phdr **cur = 0;
    int ncur = 0;
    if (!phdrs)
        return;
    for (s = top; s; s = s->next) {
        struct osec *o;
        int i;
        if (s->kind != S_OSEC || s->os->discard)
            continue;
        o = s->os;
        if (!o->nph) {
            o->ph = cur;
            o->nph = ncur;
            continue;
        }
        o->ph = xmalloc((long)o->nph * (long)sizeof(struct phdr *));
        ncur = 0;
        for (i = 0; i < o->nph; i++) {
            struct phdr *p;
            if (strcmp(o->phname[i], "NONE") == 0)
                continue;
            for (p = phdrs; p && strcmp(p->name, o->phname[i]) != 0; p = p->next)
                ;
            if (!p)
                fatal("PHDRS に無い program header", o->phname[i]);
            o->ph[ncur++] = p;
        }
        o->nph = ncur;
        cur = o->ph;
    }
}

static int in_phdr(struct osec *o, struct phdr *p)
{
    int i;
    for (i = 0; i < o->nph; i++)
        if (o->ph[i] == p)
            return 1;
    return 0;
}

/* 節の中身を書く。PHDRS があれば，PT_LOAD の中の節はファイルの位置と番地の
 * 差を揃える (FILEHDR の PT_LOAD は位置 0 から始まる) */
static void write_sections(u32 hdrsize)
{
    int i;
    struct phdr *p;
    for (p = phdrs; p; p = p->next) {
        struct osec *first = 0;
        for (i = 0; i < nolist; i++)
            if (in_phdr(olist[i], p)) {
                first = olist[i];
                break;
            }
        p->nsec = 0;
        p->offset = (u32)-1;
        if (p->type != PT_LOAD)
            continue;
        if (p->filehdr) {
            if (!first)
                p->vaddr = 0;
            else if (first->vma < hdrsize)
                fatal("FILEHDR の PT_LOAD の先頭の節が見出しと重なる", first->name);
            else
                p->vaddr = first->vma - hdrsize;
            p->offset = 0;
        } else if (first)
            p->vaddr = first->vma;
    }
    for (i = 0; i < nolist; i++) {
        struct osec *o = olist[i];
        struct phdr *load = 0;
        int k;
        for (k = 0; k < o->nph; k++)
            if (o->ph[k]->type == PT_LOAD)
                load = o->ph[k];
        if (load) {
            u32 target;
            if (load->offset == (u32)-1) {
                /* ファイルの位置を番地と 4 KiB を法として揃える */
                target = (u32)opos + ((o->vma - (u32)opos) & 0xfff);
                load->offset = target - (o->vma - load->vaddr);
            } else
                target = load->offset + (o->vma - load->vaddr);
            if ((long)target < opos)
                fatal("節のファイルの位置が前の節と重なる", o->name);
            while (opos < (long)target)
                out1(0);
        } else
            outpad(o->maxalign > 4 ? (long)o->maxalign : 4);
        o->offset = opos;
        if (o->type != SHT_NOBITS && o->data) {
            long k2;
            for (k2 = 0; k2 < (long)o->size; k2++)
                out1(o->data[k2]);
        }
    }
}

/* PHDRS の各項の値を，含む節から決める */
static void fill_phdrs(long phoff, u32 hdrsize)
{
    struct phdr *p;
    long at = phoff;
    int i;
    for (p = phdrs; p; p = p->next) {
        u32 lo = 0, hi = 0, fhi = 0, flo = 0, fl = PF_R, al = 1;
        int any = 0;
        struct osec *first = 0;
        for (i = 0; i < nolist; i++) {
            struct osec *o = olist[i];
            u32 fend;
            if (!in_phdr(o, p))
                continue;
            if (!any) {
                lo = o->vma;
                flo = (u32)o->offset;
                first = o;
            }
            any = 1;
            if (o->vma + o->size > hi)
                hi = o->vma + o->size;
            fend = (u32)o->offset + (o->type == SHT_NOBITS ? 0 : o->size);
            if (fend > fhi)
                fhi = fend;
            if (o->flags & SHF_WRITE)
                fl |= PF_W;
            if (o->flags & SHF_EXECINSTR)
                fl |= PF_X;
            if (o->maxalign > al)
                al = o->maxalign;
        }
        if (p->type == PT_LOAD) {
            al = 0x1000;
            if (p->filehdr) {
                flo = 0;
                lo = p->vaddr;
                if (!any)
                    hi = fhi = hdrsize;
            }
        } else if (p->type == PT_PHDR) {
            flo = (u32)phoff;
            fhi = flo + (u32)(nphdrs * 32);
            lo = 0;
            hi = fhi - flo;
            al = 4;
        }
        if (!any && p->type != PT_PHDR && !(p->type == PT_LOAD && p->filehdr))
            lo = hi = flo = fhi = 0;
        patch32(at, p->type);
        patch32(at + 4, flo);
        patch32(at + 8, lo);
        patch32(at + 12, first ? (lo + (first->lma - first->vma)) & 0xffffffffUL : lo);
        patch32(at + 16, fhi - flo);
        patch32(at + 20, hi - lo);
        patch32(at + 24, p->flagexpr ? (u32)eval(p->flagexpr).v : fl);
        patch32(at + 28, al);
        at += 32;
    }
}

static void writeelf(u32 entry)
{
    struct strtab shstr, str;
    struct osec *o;
    int n = nolist, i, nph = 0, shnum, stridx, shstridx, nlocal = 0, nsyms = 0;
    long phoff, shoff, symoff = 0, symsize = 0, stroff = 0, shstroff;
    long symname = 0, strname = 0, shstrname;
    u32 hdrsize;
    memset(&shstr, 0, sizeof shstr);
    memset(&str, 0, sizeof str);
    stradd(&shstr, "");
    stradd(&str, "");
    for (i = 0; i < n; i++) {
        olist[i]->nameoff = stradd(&shstr, olist[i]->name);
        if (olist[i]->size)
            nph++;
    }
    if (phdrs)
        nph = nphdrs;
    if (stripall) {
        stridx = 0;
        shstridx = n + 1;
        shnum = n + 2;
    } else {
        stridx = n + 2;
        shstridx = n + 3;
        shnum = n + 4;
    }
    hdrsize = 52 + 32 * (u32)nph;

    /* ELF 見出し */
    outn(0x464c457fUL, 4);
    out1(1);
    out1(1);
    out1(1);
    out1(0);
    outn(0, 4);
    outn(0, 4);
    outn(shared ? 3 : 2, 2);    /* ET_DYN / ET_EXEC */
    outn(243, 2);               /* EM_RISCV */
    outn(1, 4);
    outn(entry, 4);
    outn(52, 4);                /* e_phoff */
    outn(0, 4);                 /* e_shoff (後で書く) */
    outn(0, 4);                 /* e_flags: soft-float，RVC なし */
    outn(52, 2);
    outn(32, 2);
    outn((u32)nph, 2);
    outn(40, 2);
    outn((u32)shnum, 2);
    outn((u32)shstridx, 2);
    phoff = opos;
    for (i = 0; i < nph * 32; i++)
        out1(0);

    if (phdrs) {
        write_sections(hdrsize);
        fill_phdrs(phoff, hdrsize);
    } else {
        long at = phoff;
        for (i = 0; i < n; i++) {
            o = olist[i];
            outpad(o->maxalign > 4 ? (long)o->maxalign : 4);
            o->offset = opos;
            if (o->type != SHT_NOBITS && o->data) {
                long k;
                for (k = 0; k < (long)o->size; k++)
                    out1(o->data[k]);
            }
        }
        /* program header: 大きさのある出力節ごとに PT_LOAD */
        for (i = 0; i < n; i++) {
            u32 fl = PF_R;
            o = olist[i];
            if (!o->size)
                continue;
            if (o->flags & SHF_WRITE)
                fl |= PF_W;
            if (o->flags & SHF_EXECINSTR)
                fl |= PF_X;
            patch32(at, PT_LOAD);
            patch32(at + 4, (u32)o->offset);
            patch32(at + 8, o->vma);
            patch32(at + 12, o->lma);
            patch32(at + 16, o->type == SHT_NOBITS ? 0 : o->size);
            patch32(at + 20, o->size);
            patch32(at + 24, fl);
            patch32(at + 28, o->maxalign);
            at += 32;
        }
    }

    /* 記号表: 空・局所 (入力の局所の記号)・大域 */
    if (!stripall) {
        outpad(4);
        symoff = opos;
        outn(0, 4); outn(0, 4); outn(0, 4); out1(0); out1(0); outn(0, 2);
        nsyms = 1;
        for (i = 0; i < nfiles; i++) {
            struct ifile *f = files[i];
            int k;
            for (k = 1; k < f->nsym; k++) {
                struct lsym *ls = &f->syms[k];
                struct isec *x;
                if (ls->bind != STB_LOCAL || ls->type == STT_SECTION || ls->type == STT_FILE
                    || !ls->name[0] || is_local_name(ls->name))
                    continue;
                if (ls->shndx == SHN_ABS) {
                    outsym(&str, ls->name, ls->value, ls->size, STB_LOCAL, ls->type, 0, SHN_ABS);
                    nsyms++;
                    continue;
                }
                if (ls->shndx >= f->nsec || !(x = f->secs[ls->shndx]) || x->discard || !x->out)
                    continue;
                outsym(&str, ls->name, (x->addr + ls->value) & 0xffffffffUL, ls->size,
                       STB_LOCAL, ls->type, 0, x->out->shndx);
                nsyms++;
            }
        }
        nlocal = nsyms;
        {
            int h;
            struct gsym *g;
            for (h = 0; h < 4096; h++)
                for (g = gtab[h]; g; g = g->next) {
                    int shndx;
                    if (!g->defined) {
                        if (!g->referenced)
                            continue;
                        shndx = SHN_UNDEF;
                    } else if (g->sec)
                        shndx = g->sec->discard || !g->sec->out || !g->sec->out->shndx
                            ? SHN_ABS : g->sec->out->shndx;
                    else
                        shndx = osec_index(g->osec);
                    outsym(&str, g->name, g->defined ? symaddr(g) : 0, g->size,
                           g->weak ? STB_WEAK : STB_GLOBAL, g->type, g->hidden ? 2 : 0, shndx);
                    nsyms++;
                }
        }
        symsize = opos - symoff;
        stroff = opos;
        for (i = 0; i < str.size; i++)
            out1(str.buf[i]);
        symname = stradd(&shstr, ".symtab");
        strname = stradd(&shstr, ".strtab");
    }
    shstrname = stradd(&shstr, ".shstrtab");
    shstroff = opos;
    for (i = 0; i < shstr.size; i++)
        out1(shstr.buf[i]);
    outpad(4);
    shoff = opos;
    for (i = 0; i < 10; i++)
        outn(0, 4);
    for (i = 0; i < n; i++) {
        o = olist[i];
        outn((u32)o->nameoff, 4);
        outn((u32)o->type, 4);
        outn(o->flags, 4);
        outn(o->vma, 4);
        outn((u32)o->offset, 4);
        outn(o->size, 4);
        outn(o->link ? (u32)o->link->shndx : 0, 4);
        outn(o->info, 4);
        outn(o->maxalign, 4);
        outn(o->entsize, 4);
    }
    if (!stripall) {
        outn((u32)symname, 4); outn(SHT_SYMTAB, 4); outn(0, 4); outn(0, 4);
        outn((u32)symoff, 4); outn((u32)symsize, 4); outn((u32)stridx, 4);
        outn((u32)nlocal, 4); outn(4, 4); outn(16, 4);
        outn((u32)strname, 4); outn(SHT_STRTAB, 4); outn(0, 4); outn(0, 4);
        outn((u32)stroff, 4); outn((u32)str.size, 4); outn(0, 4); outn(0, 4);
        outn(1, 4); outn(0, 4);
    }
    outn((u32)shstrname, 4); outn(SHT_STRTAB, 4); outn(0, 4); outn(0, 4);
    outn((u32)shstroff, 4); outn((u32)shstr.size, 4); outn(0, 4); outn(0, 4);
    outn(1, 4); outn(0, 4);
    patch32(32, (u32)shoff);
}

static void writeout(const char *outname)
{
    FILE *f = fopen(outname, "wb");
    if (!f)
        fatal("書けない", outname);
    if ((long)fwrite(obuf, 1, opos, f) != opos)
        fatal("書込みに失敗した", outname);
    if (fclose(f) != 0)
        fatal("書込みに失敗した", outname);
}

/* ---- -r: 再配置可能オブジェクトを出す ----
 * 入力の節を名前ごとにまとめ (最初に現れた順)，番地は 0 のまま節の中の位置を
 * 決める。局所の記号はすべて (.L で始まるものも。%pcrel_lo が引く) 残し，
 * 再配置は出力の節の記号か記号へ付け替えて出す。COMMON は COMMON のまま
 * 残す。台本は EXTERN だけを使い，SECTIONS は持たない */
static struct osec **rsecs;
static int nrsecs;

static void rlayout(void)
{
    int i, k;
    for (i = 0; i < nfiles; i++) {
        struct ifile *f = files[i];
        for (k = 0; k < f->nsec; k++) {
            struct isec *x = f->secs[k];
            struct osec *o = 0;
            int j;
            if (!x)
                continue;
            for (j = 0; j < nrsecs; j++)
                if (strcmp(rsecs[j]->name, x->name) == 0)
                    o = rsecs[j];
            if (!o) {
                o = new_osec(x->name);
                o->type = x->type;
                o->flags = x->flags;
                rsecs = xrealloc(rsecs, (long)(nrsecs + 1) * (long)sizeof(struct osec *));
                rsecs[nrsecs++] = o;
                o->shndx = nrsecs;
            }
            if (x->type != SHT_NOBITS)
                o->type = x->type;
            o->flags |= x->flags;
            if (x->align > o->maxalign)
                o->maxalign = x->align;
            x->addr = alignup(o->size, x->align);
            o->size = x->addr + x->size;
            x->out = o;
            addlist(o->body ? o->body : (o->body = mkstmt(S_INPUT)), x);
        }
    }
    for (i = 0; i < nrsecs; i++) {
        struct osec *o = rsecs[i];
        struct stmt *b = o->body;
        if (o->type == SHT_NOBITS)
            continue;
        o->data = xmalloc((long)o->size);
        for (k = 0; k < b->nlist; k++)
            if (b->list[k]->data)
                memcpy(o->data + b->list[k]->addr, b->list[k]->data, b->list[k]->size);
    }
}

static void writerel(void)
{
    struct strtab shstr, str;
    int i, k, nsyms, nlocal;
    long *relnameoff, symoff, stroff, shstroff, shoff, *reloff, *relsize;
    int **lmap;                 /* ファイルごと，入力の記号の番号から出力の番号 */
    int h;
    struct gsym *g;
    long symnameoff, strnameoff, shstrnameoff;
    memset(&shstr, 0, sizeof shstr);
    memset(&str, 0, sizeof str);
    stradd(&shstr, "");
    stradd(&str, "");
    relnameoff = xmalloc((long)(nrsecs + 1) * (long)sizeof(long));
    reloff = xmalloc((long)(nrsecs + 1) * (long)sizeof(long));
    relsize = xmalloc((long)(nrsecs + 1) * (long)sizeof(long));
    for (i = 0; i < nrsecs; i++) {
        char *rn = xmalloc((long)strlen(rsecs[i]->name) + 6);
        sprintf(rn, ".rela%s", rsecs[i]->name);
        rsecs[i]->nameoff = stradd(&shstr, rsecs[i]->name);
        relnameoff[i] = stradd(&shstr, rn);
    }
    symnameoff = stradd(&shstr, ".symtab");
    strnameoff = stradd(&shstr, ".strtab");
    shstrnameoff = stradd(&shstr, ".shstrtab");

    /* ELF 見出し */
    outn(0x464c457fUL, 4);
    out1(1); out1(1); out1(1); out1(0);
    outn(0, 4); outn(0, 4);
    outn(1, 2);                 /* ET_REL */
    outn(243, 2);
    outn(1, 4);
    outn(0, 4); outn(0, 4); outn(0, 4); outn(0, 4);
    outn(52, 2); outn(0, 2); outn(0, 2); outn(40, 2);
    outn((u32)(2 * nrsecs + 4), 2);
    outn((u32)(2 * nrsecs + 3), 2);
    for (i = 0; i < nrsecs; i++) {
        struct osec *o = rsecs[i];
        outpad(o->maxalign > 4 ? (long)o->maxalign : 4);
        o->offset = opos;
        if (o->type != SHT_NOBITS)
            for (k = 0; k < (long)o->size; k++)
                out1(o->data[k]);
    }

    /* 記号表: 空・出力の節の記号・入力の局所・大域 */
    outpad(4);
    symoff = opos;
    outsym(&str, "", 0, 0, STB_LOCAL, 0, 0, 0);
    nsyms = 1;
    for (i = 0; i < nrsecs; i++) {
        outn(0, 4); outn(0, 4); outn(0, 4); out1((STB_LOCAL << 4) | STT_SECTION); out1(0);
        outn((u32)rsecs[i]->shndx, 2);
        nsyms++;
    }
    lmap = xmalloc((long)(nfiles + 1) * (long)sizeof(int *));
    for (i = 0; i < nfiles; i++) {
        struct ifile *f = files[i];
        lmap[i] = xmalloc((long)(f->nsym + 1) * (long)sizeof(int));
        for (k = 1; k < f->nsym; k++) {
            struct lsym *ls = &f->syms[k];
            struct isec *x;
            if (ls->bind != STB_LOCAL || ls->type == STT_SECTION || ls->type == STT_FILE)
                continue;
            if (ls->shndx == SHN_ABS) {
                outsym(&str, ls->name, ls->value, ls->size, STB_LOCAL, ls->type, 0, SHN_ABS);
                lmap[i][k] = nsyms++;
                continue;
            }
            if (ls->shndx >= f->nsec || !(x = f->secs[ls->shndx]))
                continue;
            outsym(&str, ls->name, x->addr + ls->value, ls->size, STB_LOCAL, ls->type, 0,
                   x->out->shndx);
            lmap[i][k] = nsyms++;
        }
    }
    nlocal = nsyms;
    for (h = 0; h < 4096; h++)
        for (g = gtab[h]; g; g = g->next) {
            int shndx;
            u32 value, size = g->size;
            if (!g->defined && !g->common && !g->referenced)
                continue;
            if (g->defined) {
                shndx = g->sec ? g->sec->out->shndx : SHN_ABS;
                value = symaddr(g);
            } else if (g->common) {
                shndx = SHN_COMMON;
                value = g->calign ? g->calign : 4;
                size = g->common;
            } else {
                shndx = SHN_UNDEF;
                value = 0;
            }
            g->outidx = nsyms++;
            outsym(&str, g->name, value, size, g->weak ? STB_WEAK : STB_GLOBAL,
                   g->common && !g->defined ? STT_OBJECT : g->type, g->hidden ? 2 : 0, shndx);
        }
    {
        long symsize = opos - symoff;
        stroff = opos;
        for (k = 0; k < str.size; k++)
            out1(str.buf[k]);

        /* 再配置 */
        for (i = 0; i < nrsecs; i++) {
            struct stmt *b = rsecs[i]->body;
            outpad(4);
            reloff[i] = opos;
            for (k = 0; b && k < b->nlist; k++) {
                struct isec *x = b->list[k];
                int fi, r;
                for (fi = 0; fi < nfiles && files[fi] != x->file; fi++)
                    ;
                for (r = 0; r < x->nrel; r++) {
                    unsigned char *e = x->rel + r * 12;
                    u32 off = rd32(e), info = rd32(e + 4);
                    long addend = sext(rd32(e + 8), 32);
                    int sk = (int)(info >> 8), ns;
                    struct lsym *ls;
                    if ((info & 0xff) == R_RISCV_NONE)
                        continue;
                    if (sk <= 0 || sk >= x->file->nsym)
                        fatal("再配置の記号の番号が範囲外", x->file->name);
                    ls = &x->file->syms[sk];
                    if (ls->g)
                        ns = ls->g->outidx;
                    else if (ls->type == STT_SECTION) {
                        struct isec *t = ls->shndx < x->file->nsec ? x->file->secs[ls->shndx] : 0;
                        if (!t)
                            fatal("割り付けない節への再配置", x->file->name);
                        ns = t->out->shndx;  /* 節の記号は出力の節の番号と同じ位置 */
                        addend += (long)t->addr;
                    } else {
                        ns = lmap[fi][sk];
                        if (!ns)
                            fatal("再配置が出力しない記号を引く", ls->name);
                    }
                    outn(x->addr + off, 4);
                    outn(((u32)ns << 8) | (info & 0xff), 4);
                    outn((u32)addend, 4);
                }
            }
            relsize[i] = opos - reloff[i];
        }
        shstroff = opos;
        for (k = 0; k < shstr.size; k++)
            out1(shstr.buf[k]);
        outpad(4);
        shoff = opos;
        for (k = 0; k < 10; k++)
            outn(0, 4);
        for (i = 0; i < nrsecs; i++) {
            struct osec *o = rsecs[i];
            outn((u32)o->nameoff, 4); outn((u32)o->type, 4); outn(o->flags, 4); outn(0, 4);
            outn((u32)o->offset, 4); outn(o->size, 4); outn(0, 4); outn(0, 4);
            outn(o->maxalign, 4); outn(0, 4);
        }
        for (i = 0; i < nrsecs; i++) {
            outn((u32)relnameoff[i], 4); outn(SHT_RELA, 4); outn(0x40, 4); outn(0, 4);
            outn((u32)reloff[i], 4); outn((u32)relsize[i], 4);
            outn((u32)(2 * nrsecs + 1), 4); outn((u32)rsecs[i]->shndx, 4);
            outn(4, 4); outn(12, 4);
        }
        outn((u32)symnameoff, 4); outn(SHT_SYMTAB, 4); outn(0, 4); outn(0, 4);
        outn((u32)symoff, 4); outn((u32)symsize, 4); outn((u32)(2 * nrsecs + 2), 4);
        outn((u32)nlocal, 4); outn(4, 4); outn(16, 4);
        outn((u32)strnameoff, 4); outn(SHT_STRTAB, 4); outn(0, 4); outn(0, 4);
        outn((u32)stroff, 4); outn((u32)str.size, 4); outn(0, 4); outn(0, 4);
        outn(1, 4); outn(0, 4);
        outn((u32)shstrnameoff, 4); outn(SHT_STRTAB, 4); outn(0, 4); outn(0, 4);
        outn((u32)shstroff, 4); outn((u32)shstr.size, 4); outn(0, 4); outn(0, 4);
        outn(1, 4); outn(0, 4);
        patch32(32, (u32)shoff);
    }
}

/* --just-symbols: ファイルの定義された大域の記号を，番地だけを持つ絶対値の
 * 記号として取り込む (vdso-syms.o) */
static void justsymbols(const char *name)
{
    long size;
    unsigned char *d = readfile(name, &size);
    u32 shoff, shnum, i;
    if (size < 52 || memcmp(d, "\177ELF\001\001", 6) != 0)
        fatal("ELF32 でない", name);
    shoff = rd32(d + 32);
    shnum = rd16(d + 48);
    if (shoff + shnum * 40 > (u32)size)
        fatal("節見出しが末尾を越える", name);
    for (i = 0; i < shnum; i++) {
        unsigned char *h = d + shoff + i * 40;
        u32 off, n, k;
        const char *str;
        if (rd32(h + 4) != SHT_SYMTAB)
            continue;
        off = rd32(h + 16);
        n = rd32(h + 20) / 16;
        str = (const char *)d + rd32(d + shoff + rd32(h + 24) * 40 + 16);
        for (k = 1; k < n; k++) {
            unsigned char *e = d + off + k * 16;
            struct gsym *g;
            if ((e[12] >> 4) == STB_LOCAL || rd16(e + 14) == SHN_UNDEF)
                continue;
            g = gsym(str + rd32(e), 1);
            if (g->defined)
                fatal("記号の多重定義", g->name);
            g->defined = 1;
            g->sec = 0;
            g->value = rd32(e + 4);
            g->size = rd32(e + 8);
            g->type = e[12] & 15;
        }
    }
}

int main(int argc, char **argv)
{
    const char *outname = "a.out", *scriptfile = 0;
    char *script;
    int i, k;
    u32 entry = 0;
    int hasentry = 0, relocatable = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-o") == 0 && i + 1 < argc)
            outname = argv[++i];
        else if (strcmp(a, "-T") == 0 && i + 1 < argc)
            scriptfile = argv[++i];
        else if (strncmp(a, "-T", 2) == 0 && a[2])
            scriptfile = a + 2;
        else if (strcmp(a, "-e") == 0 && i + 1 < argc)
            entryname = argv[++i];
        else if (strcmp(a, "-r") == 0 || strcmp(a, "--relocatable") == 0)
            relocatable = 1;
        else if (strcmp(a, "-s") == 0 || strcmp(a, "--strip-all") == 0)
            stripall = 1;
        else if (strcmp(a, "--build-id") == 0 || strcmp(a, "--build-id=sha1") == 0)
            buildid = 1;
        else if (strcmp(a, "-shared") == 0 || strcmp(a, "--shared") == 0)
            shared = 1;
        else if (strcmp(a, "--whole-archive") == 0)
            whole = 1;
        else if (strcmp(a, "--no-whole-archive") == 0)
            whole = 0;
        else if ((strcmp(a, "--just-symbols") == 0 || strcmp(a, "-R") == 0) && i + 1 < argc)
            justsymbols(argv[++i]);
        else if (strncmp(a, "--just-symbols=", 15) == 0)
            justsymbols(a + 15);
        else if ((strcmp(a, "-soname") == 0 || strcmp(a, "-h") == 0) && i + 1 < argc)
            soname = argv[++i];
        else if (strncmp(a, "-soname=", 8) == 0)
            soname = a + 8;
        else if ((strcmp(a, "-z") == 0 || strcmp(a, "-m") == 0 || strcmp(a, "-L") == 0
                  || strcmp(a, "-Map") == 0
                  || strcmp(a, "-u") == 0 || strcmp(a, "-y") == 0) && i + 1 < argc)
            i++;    /* 別の引数を取る選択肢 (-z noexecstack など) は引数ごと無視する */
        else if (a[0] == '-')
            ;   /* -melf32lriscv / --no-warn-rwx-segments / --start-group などは無視する */
        else
            readobj(a);
    }
    if (nfiles == 0 && !archives && !relocatable)
        fatal("入力が無い", 0);
    if (scriptfile) {
        long sz;
        script = (char *)readfile(scriptfile, &sz);
        script = xrealloc(script, sz + 1);
        script[sz] = 0;
        parse_script(script, scriptfile);
    } else if (!relocatable)
        parse_script(deftext, "(既定の台本)");
    pull_members();
    if (relocatable) {
        rlayout();
        writerel();
        writeout(outname);
        return 0;
    }
    makecommon();
    if (shared)
        make_dynamic();
    if (buildid)
        make_buildid();
    assign_all();
    resolve_phdrs();
    {
        struct stmt *s;
        for (s = top; s; s = s->next)
            if (s->kind == S_OSEC && !s->os->discard)
                osec_attrs(s->os);
    }
    /* 前方の参照 (後ろの記号や SIZEOF) が落ち着くまで割付けを繰り返す。
     * 値が変わらなくなったら，ASSERT と未定義の検査のためにもう 1 回回す */
    {
        u32 prev = 0, cur;
        for (k = 1;; k++) {
            lpass = k;
            layout();
            cur = layout_digest();
            if (k > 1 && cur == prev)
                break;
            if (k >= 64)
                fatal("台本の記号の値が定まらない (循環した定義)", 0);
            prev = cur;
        }
        final = 1;
        layout();
    }
    number_sections();
    if (shared)
        fill_dynamic();
    fill_sections();
    if (nundef)
        fatal("未定義の参照がある", 0);
    if (entryname) {
        struct gsym *g = gsym(entryname, 0);
        if (g && g->defined) {
            entry = symaddr(g);
            hasentry = 1;
        } else if (scriptfile || strcmp(entryname, "_start") != 0)
            fatal("入口の記号が無い", entryname);
    }
    if (!hasentry) {
        struct osec *t = findosec(".text");
        if (t)
            entry = t->vma;
    }
    writeelf(entry);
    if (s_buildid && s_buildid->out) {
        long at = s_buildid->out->offset + (long)(s_buildid->addr - s_buildid->out->vma) + 16;
        sha1(obuf, opos, obuf + at);
    }
    writeout(outname);
    return 0;
}
