/* nar1.c --- 書庫 (docs/stage018-ncc.md 6.3)
 *
 *   nar1 <操作>[<修飾>...] <書庫> [<メンバ>...]
 *
 *   r    メンバを足す / 同じ名前のものを差し替える (書庫が無ければ作る)
 *   t    メンバの名前を並べる
 *   修飾 c (作る時に黙る)・D (時刻・uid・gid を 0 に，権限を 644 に)・
 *        P (メンバの名前に経路をそのまま使う)・T (thin)・s (記号表を書く)・
 *        S (記号表を書かない)
 *
 * Linux 5.4 の Kbuild が呼ぶ形 `ar cDPrST` (built-in.a) と `ar cDPrsT`
 * (lib.a) を持つ。どちらも先に `rm -f` してから作る。
 *
 * **形式は GNU ar に合わせる。** verify 層の readelf / nm がそのまま読める。
 *
 *   書庫    "!<arch>\n" (thin なら "!<thin>\n") に続けてメンバ。
 *           メンバは 60 バイトの頭 + 中身，中身は偶数境界へ '\n' で詰める
 *   頭      名前 16・時刻 12・uid 6・gid 6・権限 8 (8 進)・大きさ 10・"`\n"
 *   "/"     記号表。記号の数 (4 バイト big endian)，その数だけの
 *           メンバの頭の位置 (同)，NUL で終わる名前の並び
 *   "//"    長い名前の表。名前を "/\n" で区切って並べ，メンバの頭には
 *           "/<表の中の位置>" を書く
 *
 * thin の書庫はメンバの中身を持たず，頭の大きさだけを持つ。名前は書庫の
 * ある場所からの相対の経路で，長さに関わらずすべて "//" の表に置く。
 * thin の書庫に thin の書庫を足すと，その中のメンバを展開して足す
 * (built-in.a は下の階層の built-in.a を並べて作る)。
 *
 * 記号表の記号は，メンバの ELF32 の記号表のうち，定義された大域と weak
 * の記号 (COMMON を含む) である。
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む (同 5.1)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HDRLEN 60
#define STB_GLOBAL 1
#define STB_WEAK 2
#define SHT_SYMTAB 2
#define SHN_UNDEF 0

struct member {
    char *path;                 /* 書庫の場所からの経路 (thin) または名前 */
    char *src;                  /* 読む時の経路 (今の作業場所から) */
    unsigned char *data;        /* thin でなければ中身 */
    long size;
    long hdroff;                /* 書き出した頭の位置 */
    long nameoff;               /* "//" の中の位置 (-1 なら頭に直に書く) */
};

static struct member *mem;
static int nmem, cmem;
static int thin, dosym = 1, quiet, det, fullpath;

static void die(const char *m, const char *a)
{
    fprintf(stderr, "nar1: %s%s%s\n", m, a ? ": " : "", a ? a : "");
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

static char *xstrdup(const char *s)
{
    char *p = xmalloc((long)strlen(s) + 1);
    strcpy(p, s);
    return p;
}

/* ファイルを丸ごと読む。無ければ 0 */
static unsigned char *readfile(const char *name, long *size)
{
    FILE *f = fopen(name, "rb");
    unsigned char *p;
    long n = 0, cap = 65536, r;
    if (!f)
        return 0;
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

/* 経路の書庫の場所 (最後の '/' まで。無ければ "") */
static char *dirof(const char *path)
{
    const char *s = strrchr(path, '/');
    char *d;
    if (!s)
        return xstrdup("");
    d = xmalloc((long)(s - path) + 2);
    memcpy(d, path, (size_t)(s - path + 1));
    return d;
}

static char *join(const char *dir, const char *name)
{
    char *p;
    if (name[0] == '/' || !dir[0])
        return xstrdup(name);
    p = xmalloc((long)(strlen(dir) + strlen(name)) + 1);
    strcpy(p, dir);
    strcat(p, name);
    return p;
}

/* 経路を '/' で区切った要素に分け，"." を除き ".." を前の要素と打ち消す。
 * 要素は path の写しの中を指す。要素の数を返す */
static int segments(const char *path, char ***segp)
{
    char *copy = xstrdup(path), *p = copy;
    char **seg = xmalloc((long)(strlen(path) + 2) * (long)sizeof(char *));
    int n = 0;
    while (*p) {
        char *s = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = 0;
        if (!*s || strcmp(s, ".") == 0)
            continue;
        if (strcmp(s, "..") == 0 && n > 0 && strcmp(seg[n - 1], "..") != 0) {
            n--;
            continue;
        }
        seg[n++] = s;
    }
    *segp = seg;
    return n;
}

static char *joinseg(int abs, char **seg, int from, int n, int up)
{
    long len = 2 + 3L * up;
    int i;
    char *out;
    for (i = from; i < n; i++)
        len += (long)strlen(seg[i]) + 1;
    out = xmalloc(len);
    if (abs)
        strcat(out, "/");
    for (i = 0; i < up; i++)
        strcat(out, "../");
    for (i = from; i < n; i++) {
        if (i > from)
            strcat(out, "/");
        strcat(out, seg[i]);
    }
    return out;
}

/* "a/b/../c" や "./x" を詰める。書庫の中の名前を揃えるため */
static char *normalize(const char *path)
{
    char **seg;
    int n = segments(path, &seg);
    return joinseg(path[0] == '/', seg, 0, n, 0);
}

/* 場所 fromdir から to への相対の経路 */
static char *relpath(const char *fromdir, const char *to)
{
    char **a, **b;
    int na, nb, common = 0;
    if (fromdir[0] && (fromdir[0] == '/') != (to[0] == '/'))
        die("絶対の経路と相対の経路は混ぜられない", to);
    na = segments(fromdir, &a);
    nb = segments(to, &b);
    while (common < na && common < nb && strcmp(a[common], b[common]) == 0)
        common++;
    return joinseg(0, b, common, nb, na - common);
}

static void addmember(const char *path, const char *src, unsigned char *data, long size)
{
    int i;
    for (i = 0; i < nmem; i++)
        if (strcmp(mem[i].path, path) == 0) {
            /* 差し替え */
            mem[i].src = xstrdup(src);
            mem[i].data = data;
            mem[i].size = size;
            return;
        }
    if (nmem == cmem) {
        struct member *m;
        cmem = cmem ? cmem * 2 : 64;
        m = xmalloc((long)cmem * (long)sizeof(struct member));
        if (nmem)
            memcpy(m, mem, (size_t)nmem * sizeof(struct member));
        free(mem);
        mem = m;
    }
    mem[nmem].path = xstrdup(path);
    mem[nmem].src = xstrdup(src);
    mem[nmem].data = data;
    mem[nmem].size = size;
    nmem++;
}

static long field(const unsigned char *h, int off, int len)
{
    char b[16];
    memcpy(b, h + off, (size_t)len);
    b[len] = 0;
    return strtol(b, 0, 10);
}

/* 書庫を読み，メンバを cb に渡す。経路は書庫の場所からのもの */
static void read_archive(const char *name, unsigned char *p, long size,
                         void (*cb)(const char *arname, const char *mname,
                                    unsigned char *data, long msize, int isthin))
{
    int isthin;
    long off = 8;
    const char *names = 0;
    long nnames = 0;
    if (size >= 8 && memcmp(p, "!<arch>\n", 8) == 0)
        isthin = 0;
    else if (size >= 8 && memcmp(p, "!<thin>\n", 8) == 0)
        isthin = 1;
    else
        die("書庫でない", name);
    while (off + HDRLEN <= size) {
        const unsigned char *h = p + off;
        long msize = field(h, 48, 10);
        char mname[4096];
        int special = 0;
        if (h[58] != '`' || h[59] != '\n')
            die("メンバの頭が壊れている", name);
        if (h[0] == '/' && (h[1] == ' ' || (h[1] == '/' && h[2] == ' ')))
            special = 1;
        if (special && h[1] == '/') {
            names = (const char *)(p + off + HDRLEN);
            nnames = msize;
        }
        if (!special) {
            if (h[0] == '/') {
                long no = field(h, 1, 15), k = 0;
                if (!names || no >= nnames)
                    die("長い名前の表が無い", name);
                while (no + k < nnames && names[no + k] != '\n' && k < 4095)
                    k++;
                memcpy(mname, names + no, (size_t)k);
                if (k > 0 && mname[k - 1] == '/')
                    k--;
                mname[k] = 0;
            } else {
                int k = 0;
                while (k < 16 && h[k] != '/' && h[k] != ' ')
                    k++;
                memcpy(mname, h, (size_t)k);
                mname[k] = 0;
            }
            cb(name, mname, isthin ? 0 : p + off + HDRLEN, msize, isthin);
        }
        off += HDRLEN;
        if (!isthin || special)
            off += msize + (msize & 1);
    }
}

/* 足す対象の書庫 (archive) の場所 */
static char *ardir;

static void add_existing(const char *arname, const char *mname, unsigned char *data,
                         long msize, int isthin)
{
    char *src = join(ardir, mname);
    (void)arname;
    (void)isthin;
    addmember(mname, src, data, msize);
}

/* thin の書庫を足す: 中のメンバを，足す書庫の場所からの経路にして足す */
static void add_nested(const char *arname, const char *mname, unsigned char *data,
                       long msize, int isthin)
{
    char *d = dirof(arname);
    char *src = normalize(join(d, mname));
    (void)data;
    if (!isthin)
        die("thin の書庫に普通の書庫は足せない", arname);
    addmember(relpath(ardir, src), src, 0, msize);
}

static void add_file(const char *file)
{
    long size;
    unsigned char *p = readfile(file, &size);
    if (!p)
        die("開けない", file);
    if (thin) {
        if (size >= 8 && memcmp(p, "!<thin>\n", 8) == 0) {
            read_archive(file, p, size, add_nested);
            free(p);
            return;
        }
        addmember(relpath(ardir, normalize(file)), file, 0, size);
        free(p);
        return;
    }
    {
        const char *base = fullpath ? file : (strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
        addmember(base, file, p, size);
    }
}

/* ---- 記号表 ---- */

struct symlist {
    char *names;
    long nlen, ncap;
    int *owner;                 /* 記号ごとのメンバの番号 */
    long n, cap;
};

static struct symlist syms;

static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16)
           | ((unsigned long)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void addsym(const char *s, int owner)
{
    long l = (long)strlen(s) + 1;
    if (syms.nlen + l > syms.ncap) {
        char *q;
        syms.ncap = (syms.ncap + l) * 2 + 4096;
        q = xmalloc(syms.ncap);
        if (syms.nlen)
            memcpy(q, syms.names, (size_t)syms.nlen);
        free(syms.names);
        syms.names = q;
    }
    memcpy(syms.names + syms.nlen, s, (size_t)l);
    syms.nlen += l;
    if (syms.n == syms.cap) {
        int *o;
        syms.cap = syms.cap ? syms.cap * 2 : 256;
        o = xmalloc(syms.cap * (long)sizeof(int));
        if (syms.n)
            memcpy(o, syms.owner, (size_t)syms.n * sizeof(int));
        free(syms.owner);
        syms.owner = o;
    }
    syms.owner[syms.n++] = owner;
}

/* ELF32 の再配置可能オブジェクトの定義された大域の記号を拾う。ELF でなければ何もしない */
static void scan_elf(const unsigned char *p, long size, int owner)
{
    unsigned long shoff;
    unsigned shentsize, shnum, i;
    if (size < 52 || memcmp(p, "\177ELF\001\001", 6) != 0)
        return;
    shoff = rd32(p + 32);
    shentsize = rd16(p + 46);
    shnum = rd16(p + 48);
    if (shoff + (unsigned long)shnum * shentsize > (unsigned long)size)
        die("節の表が壊れている", mem[owner].path);
    for (i = 0; i < shnum; i++) {
        const unsigned char *sh = p + shoff + i * shentsize;
        unsigned long off, sz, link, ent, k;
        const unsigned char *str;
        if (rd32(sh + 4) != SHT_SYMTAB)
            continue;
        off = rd32(sh + 16);
        sz = rd32(sh + 20);
        link = rd32(sh + 24);
        ent = rd32(sh + 36);
        if (ent == 0 || link >= shnum)
            die("記号表が壊れている", mem[owner].path);
        str = p + rd32(p + shoff + link * shentsize + 16);
        for (k = 1; k < sz / ent; k++) {
            const unsigned char *s = p + off + k * ent;
            int bind = s[12] >> 4;
            unsigned shndx = rd16(s + 14);
            if ((bind == STB_GLOBAL || bind == STB_WEAK) && shndx != SHN_UNDEF)
                addsym((const char *)str + rd32(s), owner);
        }
    }
}

/* ---- 書き出し ---- */

static void put(FILE *f, const void *p, long n)
{
    if (n > 0 && fwrite(p, 1, (size_t)n, f) != (size_t)n)
        die("書けない", 0);
}

static void header(FILE *f, const char *name, long size)
{
    char h[HDRLEN + 1];
    sprintf(h, "%-16s%-12ld%-6ld%-6ld%-8lo%-10ld`\n", name, 0L, 0L, 0L,
            det ? 0644L : 0644L, size);
    put(f, h, HDRLEN);
}

static void be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void write_archive(const char *name)
{
    FILE *f;
    long names = 0, symsize = 0, off, i;
    char *nametab;
    long nl = 0;
    int i2;

    /* 長い名前の表: thin なら全部，そうでなければ 16 バイト以上のもの */
    for (i2 = 0; i2 < nmem; i2++) {
        long l = (long)strlen(mem[i2].path);
        if (thin || l >= 16) {
            mem[i2].nameoff = names;
            names += l + 2;
        } else
            mem[i2].nameoff = -1;
    }
    nametab = xmalloc(names + 1);
    for (i2 = 0; i2 < nmem; i2++)
        if (mem[i2].nameoff >= 0) {
            sprintf(nametab + nl, "%s/\n", mem[i2].path);
            nl += (long)strlen(mem[i2].path) + 2;
        }
    if (dosym) {
        for (i2 = 0; i2 < nmem; i2++) {
            long sz;
            unsigned char *p = mem[i2].data;
            int own = p == 0;
            if (!p) {
                p = readfile(mem[i2].src, &sz);
                if (!p)
                    die("開けない", mem[i2].src);
            } else
                sz = mem[i2].size;
            scan_elf(p, sz, i2);
            if (own)
                free(p);
        }
        symsize = 4 + 4 * syms.n + syms.nlen;
    }
    /* メンバの頭の位置を決める */
    off = 8;
    if (dosym)
        off += HDRLEN + symsize + (symsize & 1);
    if (names)
        off += HDRLEN + names + (names & 1);
    for (i2 = 0; i2 < nmem; i2++) {
        mem[i2].hdroff = off;
        off += HDRLEN;
        if (!thin)
            off += mem[i2].size + (mem[i2].size & 1);
    }
    f = fopen(name, "wb");
    if (!f)
        die("作れない", name);
    put(f, thin ? "!<thin>\n" : "!<arch>\n", 8);
    if (dosym) {
        unsigned char b[4];
        header(f, "/", symsize);
        be32(b, (unsigned long)syms.n);
        put(f, b, 4);
        for (i = 0; i < syms.n; i++) {
            be32(b, (unsigned long)mem[syms.owner[i]].hdroff);
            put(f, b, 4);
        }
        put(f, syms.names, syms.nlen);
        if (symsize & 1)
            put(f, "\n", 1);
    }
    if (names) {
        header(f, "//", names);
        put(f, nametab, names);
        if (names & 1)
            put(f, "\n", 1);
    }
    for (i2 = 0; i2 < nmem; i2++) {
        char hn[32];
        if (mem[i2].nameoff >= 0)
            sprintf(hn, "/%ld", mem[i2].nameoff);
        else
            sprintf(hn, "%s/", mem[i2].path);
        header(f, hn, mem[i2].size);
        if (!thin) {
            put(f, mem[i2].data, mem[i2].size);
            if (mem[i2].size & 1)
                put(f, "\n", 1);
        }
    }
    if (fclose(f) != 0)
        die("書けない", name);
}

static void list_member(const char *arname, const char *mname, unsigned char *data,
                        long msize, int isthin)
{
    (void)arname;
    (void)data;
    (void)msize;
    (void)isthin;
    printf("%s\n", mname);
}

int main(int argc, char **argv)
{
    const char *ops, *archive;
    int op = 0, i;
    long size;
    unsigned char *old;

    if (argc < 3)
        die("使い方: nar1 <操作> <書庫> [<メンバ>...]", 0);
    ops = argv[1];
    if (ops[0] == '-')
        ops++;
    for (; *ops; ops++)
        switch (*ops) {
        case 'r':
        case 't':
            if (op)
                die("操作が 2 つある", argv[1]);
            op = *ops;
            break;
        case 'c':
            quiet = 1;
            break;
        case 'D':
            det = 1;
            break;
        case 'P':
            fullpath = 1;
            break;
        case 'T':
            thin = 1;
            break;
        case 's':
            dosym = 1;
            break;
        case 'S':
            dosym = 0;
            break;
        default: {
            char b[2];
            b[0] = *ops;
            b[1] = 0;
            die("知らない操作", b);
        }
        }
    if (!op)
        die("操作が無い", argv[1]);
    archive = argv[2];
    ardir = dirof(archive);
    old = readfile(archive, &size);
    if (op == 't') {
        if (!old)
            die("開けない", archive);
        read_archive(archive, old, size, list_member);
        return 0;
    }
    if (old) {
        int oldthin = size >= 8 && memcmp(old, "!<thin>\n", 8) == 0;
        if (oldthin != thin)
            die("thin と普通の書庫は混ぜられない", archive);
        read_archive(archive, old, size, add_existing);
    } else if (!quiet)
        fprintf(stderr, "nar1: 作る: %s\n", archive);
    for (i = 3; i < argc; i++)
        add_file(argv[i]);
    write_archive(archive);
    return 0;
}
