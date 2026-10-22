/* nobj1.c --- ELF32 の記号の一覧・写し・節の一覧 (docs/stage018-ncc.md 6.3)
 *
 *   nobj1 nm [-n] <ファイル>...
 *   nobj1 objcopy [-O binary] [-j <節>]... [-R <節>]... [-G <記号>]... [-S] <入力> [<出力>]
 *   nobj1 objdump -h <ファイル>...
 *
 * 呼ばれた名前 (argv[0]) の末尾が nm・objcopy・objdump なら，その道具として
 * 振る舞い，第 1 引数を道具の名前として読まない。Linux 5.4 の Kbuild が呼ぶ
 * 形だけを持つ。
 *
 *   nm -n vmlinux               scripts/mksysmap が System.map を作る
 *   objcopy -O binary           ld-option の試し，vmlinux.o の .modinfo
 *   objcopy -j <節> -O binary   (同)
 *   objcopy -G <記号>...        vdso の記号のうち指定以外の大域を局所にする
 *   objcopy -S                  vdso.so.dbg から記号表を除く
 *   objcopy -O binary -R <節> -S  arch/riscv/boot/Image (-S は -O binary では意味を持たない)
 *   objdump -h <書庫>           lib-ksyms の台本を作る sed が節の名前を拾う
 *
 * nm と objdump は GNU binutils と同じ書式で出す (Kbuild の sed が読む)。
 * 書庫 (thin を含む) はメンバごとに出す。
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む (同 5.1)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_OBJECT 1
#define STT_SECTION 3
#define STT_FILE 4
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define PT_LOAD 1

typedef unsigned long u32;

static void die(const char *m, const char *a)
{
    fprintf(stderr, "nobj1: %s%s%s\n", m, a ? ": " : "", a ? a : "");
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p)
        die("記憶域が足りない", 0);
    memset(p, 0, n > 0 ? n : 1);
    return p;
}

static unsigned char *readfile(const char *name, long *size)
{
    FILE *f = fopen(name, "rb");
    unsigned char *p;
    long n = 0, cap = 65536, r;
    if (!f)
        die("開けない", name);
    p = xmalloc(cap);
    while ((r = (long)fread(p + n, 1, (size_t)(cap - n), f)) > 0) {
        n += r;
        if (n == cap) {
            unsigned char *q = xmalloc(cap * 2);
            memcpy(q, p, (size_t)n);
            free(p);
            p = q;
            cap *= 2;
        }
    }
    fclose(f);
    *size = n;
    return p;
}

static void writefile(const char *name, const unsigned char *p, long n)
{
    FILE *f = fopen(name, "wb");
    if (!f)
        die("作れない", name);
    if (n > 0 && fwrite(p, 1, (size_t)n, f) != (size_t)n)
        die("書けない", name);
    if (fclose(f) != 0)
        die("書けない", name);
}

static u32 rd32(const unsigned char *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void wr32(unsigned char *p, u32 v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* ---- ELF32 を読む ---- */

struct shdr {
    u32 name, type, flags, addr, offset, size, link, info, align, entsize;
};

struct elf {
    const char *fname;
    unsigned char *p;
    long size;
    int type;
    int shnum, shstrndx;
    struct shdr *sh;
    int phnum;
    u32 phoff;
};

static void rdshdr(const unsigned char *s, struct shdr *h)
{
    h->name = rd32(s);
    h->type = rd32(s + 4);
    h->flags = rd32(s + 8);
    h->addr = rd32(s + 12);
    h->offset = rd32(s + 16);
    h->size = rd32(s + 20);
    h->link = rd32(s + 24);
    h->info = rd32(s + 28);
    h->align = rd32(s + 32);
    h->entsize = rd32(s + 36);
}

static void wrshdr(unsigned char *s, const struct shdr *h)
{
    wr32(s, h->name);
    wr32(s + 4, h->type);
    wr32(s + 8, h->flags);
    wr32(s + 12, h->addr);
    wr32(s + 16, h->offset);
    wr32(s + 20, h->size);
    wr32(s + 24, h->link);
    wr32(s + 28, h->info);
    wr32(s + 32, h->align);
    wr32(s + 36, h->entsize);
}

static int is_elf(const unsigned char *p, long size)
{
    return size >= 52 && memcmp(p, "\177ELF", 4) == 0;
}

static void open_elf(struct elf *e, const char *fname, unsigned char *p, long size)
{
    u32 shoff;
    unsigned entsize;
    int i;
    e->fname = fname;
    e->p = p;
    e->size = size;
    if (!is_elf(p, size))
        die("ELF でない", fname);
    if (p[4] != 1 || p[5] != 1)
        die("ELF32 の little endian でない", fname);
    e->type = (int)rd16(p + 16);
    e->phoff = rd32(p + 28);
    e->phnum = (int)rd16(p + 44);
    shoff = rd32(p + 32);
    entsize = rd16(p + 46);
    e->shnum = (int)rd16(p + 48);
    e->shstrndx = (int)rd16(p + 50);
    if (e->shnum && (entsize != 40 || shoff + (u32)e->shnum * 40 > (u32)size))
        die("節の表が壊れている", fname);
    e->sh = xmalloc((long)(e->shnum + 1) * (long)sizeof(struct shdr));
    for (i = 0; i < e->shnum; i++) {
        rdshdr(p + shoff + (u32)i * 40, &e->sh[i]);
        if (e->sh[i].type != SHT_NOBITS && e->sh[i].type != SHT_NULL
            && e->sh[i].offset + e->sh[i].size > (u32)size)
            die("節が壊れている", fname);
    }
}

static const char *secname(struct elf *e, int i)
{
    if (e->shstrndx <= 0 || e->shstrndx >= e->shnum)
        return "";
    return (const char *)e->p + e->sh[e->shstrndx].offset + e->sh[i].name;
}

/* 節の LMA。実行形式なら節を含む PT_LOAD の paddr から求める */
static u32 lma_of(struct elf *e, int i)
{
    int k;
    u32 a = e->sh[i].addr;
    if (e->type == ET_REL)
        return a;
    for (k = 0; k < e->phnum; k++) {
        const unsigned char *ph = e->p + e->phoff + (u32)k * 32;
        u32 vaddr = rd32(ph + 8), paddr = rd32(ph + 12), memsz = rd32(ph + 20);
        if (rd32(ph) == PT_LOAD && a >= vaddr && a < vaddr + memsz)
            return a - vaddr + paddr;
    }
    return a;
}

/* ---- 書庫 ---- */

static long arfield(const unsigned char *h, int off, int len)
{
    char b[16];
    memcpy(b, h + off, (size_t)len);
    b[len] = 0;
    return strtol(b, 0, 10);
}

/* 書庫ならメンバごとに cb を呼んで 1 を返す。書庫でなければ 0 */
static int each_member(const char *arname, unsigned char *p, long size,
                       void (*cb)(const char *name, unsigned char *p, long size))
{
    int thin;
    long off = 8;
    const char *names = 0;
    long nnames = 0;
    char dir[4096];
    const char *slash = strrchr(arname, '/');
    if (size >= 8 && memcmp(p, "!<arch>\n", 8) == 0)
        thin = 0;
    else if (size >= 8 && memcmp(p, "!<thin>\n", 8) == 0)
        thin = 1;
    else
        return 0;
    dir[0] = 0;
    if (slash && slash - arname < 4000) {
        memcpy(dir, arname, (size_t)(slash - arname + 1));
        dir[slash - arname + 1] = 0;
    }
    while (off + 60 <= size) {
        const unsigned char *h = p + off;
        long msize = arfield(h, 48, 10);
        char mname[4096];
        int special = h[0] == '/' && (h[1] == ' ' || (h[1] == '/' && h[2] == ' '));
        if (special && h[1] == '/') {
            names = (const char *)(p + off + 60);
            nnames = msize;
        }
        if (!special) {
            long k = 0;
            if (h[0] == '/') {
                long no = arfield(h, 1, 15);
                if (!names || no >= nnames)
                    die("長い名前の表が無い", arname);
                while (no + k < nnames && names[no + k] != '\n' && k < 4000)
                    k++;
                memcpy(mname, names + no, (size_t)k);
                if (k > 0 && mname[k - 1] == '/')
                    k--;
            } else {
                while (k < 16 && h[k] != '/' && h[k] != ' ')
                    k++;
                memcpy(mname, h, (size_t)k);
            }
            mname[k] = 0;
            if (thin) {
                char path[8192];
                long msz;
                unsigned char *m;
                sprintf(path, "%s%s", mname[0] == '/' ? "" : dir, mname);
                m = readfile(path, &msz);
                cb(path, m, msz);
                free(m);
            } else
                cb(mname, p + off + 60, msize);
        }
        off += 60;
        if (!thin || special)
            off += msize + (msize & 1);
    }
    return 1;
}

/* ---- nm ---- */

struct nsym {
    const char *name;
    u32 value;
    char type;
};

static int nm_numeric;
static int nm_multi;

static char symtype(struct elf *e, const unsigned char *s)
{
    int bind = s[12] >> 4, type = s[12] & 15;
    unsigned shndx = rd16(s + 14);
    char c;
    if (shndx == SHN_UNDEF)
        return bind == STB_WEAK ? (type == STT_OBJECT ? 'v' : 'w') : 'U';
    if (bind == STB_WEAK)
        return type == STT_OBJECT ? 'V' : 'W';
    if (shndx == SHN_COMMON)
        c = 'C';
    else if (shndx == SHN_ABS)
        c = 'A';
    else if (shndx >= (unsigned)e->shnum)
        c = '?';
    else {
        struct shdr *h = &e->sh[shndx];
        if (h->flags & SHF_EXECINSTR)
            c = 'T';
        else if (h->type == SHT_NOBITS && (h->flags & SHF_ALLOC))
            c = 'B';
        else if ((h->flags & SHF_ALLOC) && (h->flags & SHF_WRITE))
            c = 'D';
        else if (h->flags & SHF_ALLOC)
            c = 'R';
        else
            c = 'N';
    }
    if (bind == STB_LOCAL && c != '?')
        c = (char)(c - 'A' + 'a');
    return c;
}

static int nsym_cmp(const void *a, const void *b)
{
    const struct nsym *x = a, *y = b;
    if (nm_numeric) {
        int ux = x->type == 'U' || x->type == 'w' || x->type == 'v';
        int uy = y->type == 'U' || y->type == 'w' || y->type == 'v';
        if (ux != uy)
            return ux ? -1 : 1;
        if (x->value != y->value)
            return x->value < y->value ? -1 : 1;
    }
    return strcmp(x->name, y->name);
}

static void nm_elf(const char *fname, unsigned char *p, long size)
{
    struct elf e;
    int i, n = 0;
    struct nsym *v = 0;
    open_elf(&e, fname, p, size);
    for (i = 0; i < e.shnum; i++) {
        u32 k, cnt;
        const char *str;
        if (e.sh[i].type != SHT_SYMTAB)
            continue;
        cnt = e.sh[i].entsize ? e.sh[i].size / e.sh[i].entsize : 0;
        str = (const char *)p + e.sh[e.sh[i].link].offset;
        v = xmalloc((long)(cnt + 1) * (long)sizeof(struct nsym));
        for (k = 1; k < cnt; k++) {
            const unsigned char *s = p + e.sh[i].offset + k * 16;
            int type = s[12] & 15;
            const char *nm = str + rd32(s);
            /* .L で始まる局所の記号はアセンブラの内部のラベルとして出さない (GNU nm と同じ) */
            if (type == STT_SECTION || type == STT_FILE || !nm[0]
                || ((s[12] >> 4) == STB_LOCAL && nm[0] == '.' && nm[1] == 'L'))
                continue;
            v[n].name = nm;
            v[n].value = rd32(s + 4);
            v[n].type = symtype(&e, s);
            n++;
        }
        break;
    }
    if (nm_multi)
        printf("\n%s:\n", fname);
    if (!v) {
        fprintf(stderr, "nobj1: %s: 記号が無い\n", fname);
        return;
    }
    qsort(v, (size_t)n, sizeof(struct nsym), nsym_cmp);
    for (i = 0; i < n; i++) {
        if (v[i].type == 'U' || v[i].type == 'w' || v[i].type == 'v')
            printf("         %c %s\n", v[i].type, v[i].name);
        else
            printf("%08lx %c %s\n", v[i].value & 0xffffffffUL, v[i].type, v[i].name);
    }
    free(v);
    free(e.sh);
}

static int do_nm(int argc, char **argv)
{
    int i, nfiles = 0;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--numeric-sort") == 0)
            nm_numeric = 1;
        else if (argv[i][0] == '-')
            die("nm: 知らない選択肢", argv[i]);
        else
            nfiles++;
    }
    if (!nfiles)
        die("nm: ファイルが無い", 0);
    nm_multi = nfiles > 1;
    for (i = 0; i < argc; i++) {
        long size;
        unsigned char *p;
        if (argv[i][0] == '-')
            continue;
        p = readfile(argv[i], &size);
        if (each_member(argv[i], p, size, nm_elf)) {
            free(p);
            continue;
        }
        nm_elf(argv[i], p, size);
        free(p);
    }
    return 0;
}

/* ---- objdump -h ---- */

/* 節 i の旗の行。再配置を持つ節には RELOC を付ける */
static void flagline(struct elf *e, int i)
{
    struct shdr *h = &e->sh[i];
    char b[256];
    int k;
    b[0] = 0;
    if (h->type != SHT_NOBITS)
        strcat(b, "CONTENTS, ");
    if (h->flags & SHF_ALLOC) {
        strcat(b, "ALLOC, ");
        if (h->type != SHT_NOBITS)
            strcat(b, "LOAD, ");
    }
    for (k = 0; k < e->shnum; k++)
        if ((e->sh[k].type == SHT_RELA || e->sh[k].type == SHT_REL) && e->sh[k].info == (u32)i
            && e->type == ET_REL) {
            strcat(b, "RELOC, ");
            break;
        }
    if (!(h->flags & SHF_WRITE))
        strcat(b, "READONLY, ");
    if (h->flags & SHF_EXECINSTR)
        strcat(b, "CODE");
    else if (h->type != SHT_NOBITS && (h->flags & SHF_ALLOC))
        strcat(b, "DATA");
    else {
        long l = (long)strlen(b);
        if (l >= 2)
            b[l - 2] = 0;
    }
    printf("                  %s\n", b);
}

static int align_log(u32 a)
{
    int k = 0;
    while (a > 1) {
        a >>= 1;
        k++;
    }
    return k;
}


static void objdump_elf(const char *fname, unsigned char *p, long size)
{
    struct elf e;
    int i, idx = 0;
    open_elf(&e, fname, p, size);
    printf("\n%s:     file format elf32-littleriscv\n\n", fname);
    printf("Sections:\nIdx Name          Size      VMA       LMA       File off  Algn\n");
    for (i = 1; i < e.shnum; i++) {
        struct shdr *h = &e.sh[i];
        if (h->type == SHT_SYMTAB || h->type == SHT_STRTAB || h->type == SHT_RELA
            || h->type == SHT_REL || h->type == SHT_NULL)
            continue;
        printf("%3d %-13s %08lx  %08lx  %08lx  %08lx  2**%d\n", idx++, secname(&e, i),
               h->size & 0xffffffffUL, h->addr & 0xffffffffUL,
               lma_of(&e, i) & 0xffffffffUL, h->offset & 0xffffffffUL, align_log(h->align));
        flagline(&e, i);
    }
    free(e.sh);
}

static int do_objdump(int argc, char **argv)
{
    int i, h = 0;
    for (i = 0; i < argc; i++)
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--section-headers") == 0)
            h = 1;
        else if (argv[i][0] == '-')
            die("objdump: 知らない選択肢", argv[i]);
    if (!h)
        die("objdump: -h だけを持つ", 0);
    for (i = 0; i < argc; i++) {
        long size;
        unsigned char *p;
        if (argv[i][0] == '-')
            continue;
        p = readfile(argv[i], &size);
        if (size >= 8 && (memcmp(p, "!<arch>\n", 8) == 0 || memcmp(p, "!<thin>\n", 8) == 0)) {
            printf("In archive %s:\n", argv[i]);
            each_member(argv[i], p, size, objdump_elf);
        } else
            objdump_elf(argv[i], p, size);
        free(p);
    }
    return 0;
}

/* ---- objcopy ---- */

#define MAXLIST 256

static const char *only[MAXLIST];
static int nonly;
static const char *keepg[MAXLIST];
static int nkeepg;
static const char *removed[MAXLIST];
static int nremoved;

static int in_list(const char **l, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(l[i], s) == 0)
            return 1;
    return 0;
}

/* -O binary に入れる節: -j があればその節，無ければ割り付ける中身のある節。
 * -R の節は除く (arch/riscv/boot の Image は .note・.comment を除く) */
static int binary_selected(struct elf *e, int i)
{
    struct shdr *h = &e->sh[i];
    if (in_list(removed, nremoved, secname(e, i)) || h->type == SHT_NOBITS)
        return 0;
    if (nonly)
        return in_list(only, nonly, secname(e, i));
    return (h->flags & SHF_ALLOC) && h->size != 0;
}

/* -O binary: 選んだ節の中身を LMA の順に並べ，間を 0 で埋める */
static void copy_binary(struct elf *e, const char *out)
{
    int i, any = 0;
    u32 lo = 0, hi = 0;
    unsigned char *img;
    for (i = 1; i < e->shnum; i++) {
        struct shdr *h = &e->sh[i];
        u32 l;
        if (!binary_selected(e, i))
            continue;
        l = lma_of(e, i);
        if (!any || l < lo)
            lo = l;
        if (!any || l + h->size > hi)
            hi = l + h->size;
        any = 1;
    }
    if (!any) {
        writefile(out, (const unsigned char *)"", 0);
        return;
    }
    img = xmalloc((long)(hi - lo));
    for (i = 1; i < e->shnum; i++) {
        struct shdr *h = &e->sh[i];
        if (!binary_selected(e, i))
            continue;
        memcpy(img + (lma_of(e, i) - lo), e->p + h->offset, (size_t)h->size);
    }
    writefile(out, img, (long)(hi - lo));
    free(img);
}

/* -G: 指定以外の定義された大域・weak の記号を局所にする。局所の記号を
 * 前に並べ直し，記号表を指す再配置の記号の番号を付け替える */
static void localize(struct elf *e)
{
    int i;
    for (i = 0; i < e->shnum; i++) {
        struct shdr *h = &e->sh[i];
        u32 cnt, k, nloc = 0, j;
        unsigned char *old;
        u32 *map;
        const char *str;
        int r;
        if (h->type != SHT_SYMTAB)
            continue;
        cnt = h->entsize ? h->size / h->entsize : 0;
        str = (const char *)e->p + e->sh[h->link].offset;
        old = xmalloc((long)h->size);
        memcpy(old, e->p + h->offset, (size_t)h->size);
        for (k = 1; k < cnt; k++) {
            unsigned char *s = old + k * 16;
            int bind = s[12] >> 4;
            if (bind != STB_LOCAL && rd16(s + 14) != SHN_UNDEF
                && !in_list(keepg, nkeepg, str + rd32(s)))
                s[12] = (unsigned char)((STB_LOCAL << 4) | (s[12] & 15));
        }
        /* 局所を先に，元の順を保って並べ直す */
        map = xmalloc((long)(cnt + 1) * (long)sizeof(u32));
        j = 0;
        for (r = 0; r < 2; r++)
            for (k = 0; k < cnt; k++) {
                unsigned char *s = old + k * 16;
                int loc = (s[12] >> 4) == STB_LOCAL;
                if ((r == 0) != loc)
                    continue;
                memcpy(e->p + h->offset + j * 16, s, 16);
                map[k] = j;
                j++;
                if (r == 0)
                    nloc = j;
            }
        h->info = nloc;
        for (k = 0; k < (u32)e->shnum; k++) {
            struct shdr *rh = &e->sh[k];
            u32 m, ent = rh->type == SHT_RELA ? 12 : 8;
            if ((rh->type != SHT_RELA && rh->type != SHT_REL) || rh->link != (u32)i)
                continue;
            for (m = 0; m + ent <= rh->size; m += ent) {
                unsigned char *rp = e->p + rh->offset + m;
                u32 info = rd32(rp + 4);
                u32 sym = info >> 8;
                if (sym >= cnt)
                    die("再配置の記号の番号が範囲外", e->fname);
                wr32(rp + 4, (map[sym] << 8) | (info & 0xff));
            }
        }
        free(map);
        free(old);
    }
    /* 節の頭を書き戻す */
    for (i = 0; i < e->shnum; i++)
        wrshdr(e->p + rd32(e->p + 32) + (u32)i * 40, &e->sh[i]);
}

/* -S: 記号表とその文字列表，割り付けない節への再配置と .debug* を除く。
 * 割り付ける節の位置は変えず，残す割り付けない節を後ろに写す */
static long strip_all(struct elf *e, unsigned char **outp)
{
    int i, n = 0, *map = xmalloc((long)(e->shnum + 1) * (long)sizeof(int));
    int *keep = xmalloc((long)(e->shnum + 1) * (long)sizeof(int));
    u32 end = 52 + (u32)e->phnum * 32, off;
    unsigned char *out;
    long cap;
    for (i = 0; i < e->shnum; i++) {
        struct shdr *h = &e->sh[i];
        const char *nm = secname(e, i);
        keep[i] = 1;
        if (h->type == SHT_SYMTAB || strncmp(nm, ".debug", 6) == 0)
            keep[i] = 0;
        if ((h->type == SHT_RELA || h->type == SHT_REL) && !(h->flags & SHF_ALLOC))
            keep[i] = 0;
    }
    /* 除いた記号表の文字列表 (他から引かれていなければ) */
    for (i = 0; i < e->shnum; i++)
        if (e->sh[i].type == SHT_SYMTAB) {
            int s = (int)e->sh[i].link, used = 0, k;
            for (k = 0; k < e->shnum; k++)
                if (keep[k] && k != i && (int)e->sh[k].link == s && e->sh[k].type != SHT_NULL)
                    used = 1;
            if (s != e->shstrndx && !used)
                keep[s] = 0;
        }
    for (i = 0; i < e->shnum; i++) {
        map[i] = keep[i] ? n++ : 0;
        if (keep[i] && (e->sh[i].flags & SHF_ALLOC) && e->sh[i].type != SHT_NOBITS
            && e->sh[i].offset + e->sh[i].size > end)
            end = e->sh[i].offset + e->sh[i].size;
    }
    cap = (long)end + e->size + (long)n * 40 + 64;
    out = xmalloc(cap);
    memcpy(out, e->p, (size_t)end);
    off = end;
    for (i = 0; i < e->shnum; i++) {
        struct shdr *h = &e->sh[i];
        if (!keep[i] || (h->flags & SHF_ALLOC) || h->type == SHT_NOBITS || h->type == SHT_NULL)
            continue;
        if (h->align > 1)
            off = (off + h->align - 1) / h->align * h->align;
        memcpy(out + off, e->p + h->offset, (size_t)h->size);
        h->offset = off;
        off += h->size;
    }
    off = (off + 3) & ~(u32)3;
    wr32(out + 32, off);
    wr16(out + 48, (unsigned)n);
    wr16(out + 50, (unsigned)map[e->shstrndx]);
    for (i = 0; i < e->shnum; i++) {
        struct shdr h;
        if (!keep[i])
            continue;
        h = e->sh[i];
        if (h.link < (u32)e->shnum)
            h.link = (u32)map[h.link];
        if ((h.type == SHT_RELA || h.type == SHT_REL) && h.info < (u32)e->shnum)
            h.info = (u32)map[h.info];
        wrshdr(out + off + (u32)map[i] * 40, &h);
    }
    free(map);
    free(keep);
    *outp = out;
    return (long)(off + (u32)n * 40);
}

static int do_objcopy(int argc, char **argv)
{
    const char *in = 0, *out = 0, *ofmt = 0;
    int i, strip = 0;
    long size;
    unsigned char *p;
    struct elf e;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-O") == 0 && i + 1 < argc)
            ofmt = argv[++i];
        else if (strcmp(a, "-j") == 0 && i + 1 < argc) {
            if (nonly >= MAXLIST)
                die("objcopy: -j が多すぎる", 0);
            only[nonly++] = argv[++i];
        } else if (strcmp(a, "-R") == 0 && i + 1 < argc) {
            if (nremoved >= MAXLIST)
                die("objcopy: -R が多すぎる", 0);
            removed[nremoved++] = argv[++i];
        } else if (strcmp(a, "-G") == 0 && i + 1 < argc) {
            if (nkeepg >= MAXLIST)
                die("objcopy: -G が多すぎる", 0);
            keepg[nkeepg++] = argv[++i];
        } else if (strcmp(a, "-S") == 0 || strcmp(a, "--strip-all") == 0)
            strip = 1;
        else if (a[0] == '-')
            die("objcopy: 知らない選択肢", a);
        else if (!in)
            in = a;
        else if (!out)
            out = a;
        else
            die("objcopy: ファイルが多すぎる", a);
    }
    if (!in)
        die("objcopy: 入力が無い", 0);
    if (!out)
        out = in;
    p = readfile(in, &size);
    open_elf(&e, in, p, size);
    if (ofmt) {
        if (strcmp(ofmt, "binary") != 0)
            die("objcopy: -O は binary だけを持つ", ofmt);
        copy_binary(&e, out);
        return 0;
    }
    if (nonly || nremoved)
        die("objcopy: -j と -R は -O binary とだけ使える", 0);
    if (nkeepg)
        localize(&e);
    if (strip) {
        unsigned char *o;
        long n = strip_all(&e, &o);
        writefile(out, o, n);
        return 0;
    }
    writefile(out, p, size);
    return 0;
}

static int ends_with(const char *s, const char *t)
{
    long ls = (long)strlen(s), lt = (long)strlen(t);
    return ls >= lt && strcmp(s + ls - lt, t) == 0;
}

int main(int argc, char **argv)
{
    const char *self = argv[0];
    if (ends_with(self, "nm"))
        return do_nm(argc - 1, argv + 1);
    if (ends_with(self, "objcopy"))
        return do_objcopy(argc - 1, argv + 1);
    if (ends_with(self, "objdump"))
        return do_objdump(argc - 1, argv + 1);
    if (argc < 2)
        die("使い方: nobj1 {nm | objcopy | objdump} ...", 0);
    if (strcmp(argv[1], "nm") == 0)
        return do_nm(argc - 2, argv + 2);
    if (strcmp(argv[1], "objcopy") == 0)
        return do_objcopy(argc - 2, argv + 2);
    if (strcmp(argv[1], "objdump") == 0)
        return do_objdump(argc - 2, argv + 2);
    die("知らない道具", argv[1]);
    return 1;
}
