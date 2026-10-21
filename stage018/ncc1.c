/* ncc1.c --- psABI ilp32 の C コンパイラ (docs/stage018-ncc.md 4 章)
 *
 *   ncc1 [-o <出力.s>] [-mcmodel=medlow|medany] [<入力.i>]
 *
 * 前処理済みの C を読み，GNU as の書式のアセンブリ (nas1 の入力) を出す。
 * 入力を省くと標準入力を，-o を省くと標準出力を使う。0x04 は入力の終わり
 * (stone の pp の慣習)。
 *
 * 段の構成 (同 4.1):
 *   1. 字句解析: 単位全体を 1 度だけ字句に分け，識別子を intern する
 *   2. 構文解析と型付け: 再帰下降で型付きの構文木を作る。暗黙の型変換は
 *      ここで N_CAST として木に入れる
 *   3. 定数の畳込み: 整数定数式と番地定数を評価する
 *   4. コード生成: 関数ごとに木を辿ってアセンブリを出す。中間表現は置かない
 *   5. 出力の選別: 参照された static の関数・変数だけを出す
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む (同 5.1)。対象の
 * 値は long long (64 bit) で持ち，ホストの long の幅に依らない。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long i64;
typedef unsigned long long u64;

/* ==== 補助 ==== */

static const char *curfile = "<stdin>";
static int curline = 1;

static void fatal_at(const char *file, int line, const char *msg, const char *arg)
{
    if (arg)
        fprintf(stderr, "ncc1: %s:%d: %s: %s\n", file, line, msg, arg);
    else
        fprintf(stderr, "ncc1: %s:%d: %s\n", file, line, msg);
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p) {
        fprintf(stderr, "ncc1: 記憶域が足りない\n");
        exit(1);
    }
    memset(p, 0, n > 0 ? n : 1);
    return p;
}

static void *xrealloc(void *p, long n)
{
    p = realloc(p, n > 0 ? n : 1);
    if (!p) {
        fprintf(stderr, "ncc1: 記憶域が足りない\n");
        exit(1);
    }
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

/* 伸びる文字列 */
struct buf {
    char *p;
    long n;
    long cap;
};

static void bput(struct buf *b, const char *s, long n)
{
    if (b->n + n + 1 > b->cap) {
        long c = b->cap ? b->cap : 256;
        while (b->n + n + 1 > c)
            c *= 2;
        b->p = xrealloc(b->p, c);
        b->cap = c;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void bputs(struct buf *b, const char *s)
{
    bput(b, s, (long)strlen(s));
}

/* 64 bit の値を 10 進の文字列にする (ホストの printf の %lld に依らない) */
static char *i64str(i64 v, char *out)
{
    char tmp[32];
    int n = 0, neg = v < 0, i = 0;
    u64 u = neg ? (u64)0 - (u64)v : (u64)v;
    do {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (neg)
        out[i++] = '-';
    while (n > 0)
        out[i++] = tmp[--n];
    out[i] = 0;
    return out;
}

/* 書式付きの出力。%s (文字列)，%d (int)，%L (i64) だけを持つ */
static void bprintf(struct buf *b, const char *fmt, ...);

#include <stdarg.h>

static void bvprintf(struct buf *b, const char *fmt, va_list ap)
{
    char num[32];
    const char *p;
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            bput(b, p, 1);
            continue;
        }
        p++;
        switch (*p) {
        case 's':
            bputs(b, va_arg(ap, const char *));
            break;
        case 'd':
            bputs(b, i64str((i64)va_arg(ap, int), num));
            break;
        case 'L':
            bputs(b, i64str(va_arg(ap, i64), num));
            break;
        case '%':
            bput(b, "%", 1);
            break;
        default:
            bput(b, p - 1, 2);
            break;
        }
    }
}

static void bprintf(struct buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bvprintf(b, fmt, ap);
    va_end(ap);
}

/* ==== 字句 ==== */

enum {
    T_EOF, T_IDENT, T_NUM, T_FNUM, T_CHAR, T_STR, T_PUNCT, T_KW
};

/* 予約語 */
enum {
    K_NONE,
    K_AUTO, K_BREAK, K_CASE, K_CHAR, K_CONST, K_CONTINUE, K_DEFAULT, K_DO,
    K_DOUBLE, K_ELSE, K_ENUM, K_EXTERN, K_FLOAT, K_FOR, K_GOTO, K_IF, K_INT,
    K_LONG, K_REGISTER, K_RETURN, K_SHORT, K_SIGNED, K_SIZEOF, K_STATIC,
    K_STRUCT, K_SWITCH, K_TYPEDEF, K_UNION, K_UNSIGNED, K_VOID, K_VOLATILE,
    K_WHILE, K_INLINE, K_ATTRIBUTE, K_EXTENSION, K_ASM, K_TYPEOF, K_BOOL,
    K_RESTRICT, K_ALIGNOF, K_LABEL, K_STATIC_ASSERT, K_VA_LIST
};

struct kwdef {
    const char *name;
    int kw;
};

static const struct kwdef kwdefs[] = {
    { "auto", K_AUTO }, { "break", K_BREAK }, { "case", K_CASE }, { "char", K_CHAR },
    { "const", K_CONST }, { "__const", K_CONST }, { "__const__", K_CONST },
    { "continue", K_CONTINUE }, { "default", K_DEFAULT }, { "do", K_DO },
    { "double", K_DOUBLE }, { "else", K_ELSE }, { "enum", K_ENUM },
    { "extern", K_EXTERN }, { "float", K_FLOAT }, { "for", K_FOR },
    { "goto", K_GOTO }, { "if", K_IF }, { "int", K_INT }, { "long", K_LONG },
    { "register", K_REGISTER }, { "return", K_RETURN }, { "short", K_SHORT },
    { "signed", K_SIGNED }, { "__signed", K_SIGNED }, { "__signed__", K_SIGNED },
    { "sizeof", K_SIZEOF }, { "static", K_STATIC }, { "struct", K_STRUCT },
    { "switch", K_SWITCH }, { "typedef", K_TYPEDEF }, { "union", K_UNION },
    { "unsigned", K_UNSIGNED }, { "void", K_VOID }, { "volatile", K_VOLATILE },
    { "__volatile", K_VOLATILE }, { "__volatile__", K_VOLATILE },
    { "while", K_WHILE }, { "inline", K_INLINE }, { "__inline", K_INLINE },
    { "__inline__", K_INLINE }, { "__attribute__", K_ATTRIBUTE },
    { "__attribute", K_ATTRIBUTE }, { "__extension__", K_EXTENSION },
    { "asm", K_ASM }, { "__asm", K_ASM }, { "__asm__", K_ASM },
    { "typeof", K_TYPEOF }, { "__typeof", K_TYPEOF }, { "__typeof__", K_TYPEOF },
    { "_Bool", K_BOOL }, { "restrict", K_RESTRICT }, { "__restrict", K_RESTRICT },
    { "__restrict__", K_RESTRICT }, { "__alignof__", K_ALIGNOF },
    { "__alignof", K_ALIGNOF }, { "_Alignof", K_ALIGNOF }, { "__label__", K_LABEL },
    { "_Static_assert", K_STATIC_ASSERT }, { "__builtin_va_list", K_VA_LIST },
    { 0, 0 }
};

/* intern した名前 */
struct name {
    char *s;
    int len;
    int kw;
    struct name *next;
    struct sym *ord;        /* 通常の名前空間の今の束縛 */
    struct sym *tag;        /* タグの名前空間の今の束縛 */
    struct sym *gsym;       /* ファイルの有効範囲の変数・関数 (局所の extern が引く) */
};

static struct name *names[16384];

static unsigned long hashn(const char *s, long n)
{
    unsigned long h = 2166136261UL;
    long i;
    for (i = 0; i < n; i++) {
        h = (h ^ (unsigned char)s[i]) * 16777619UL;
        h &= 0xffffffffUL;
    }
    return h;
}

static struct name *intern(const char *s, long n)
{
    unsigned long h = hashn(s, n) % 16384;
    struct name *p;
    for (p = names[h]; p; p = p->next)
        if (p->len == n && memcmp(p->s, s, n) == 0)
            return p;
    p = xmalloc(sizeof(struct name));
    p->s = xstrndup(s, n);
    p->len = (int)n;
    p->next = names[h];
    names[h] = p;
    return p;
}

struct token {
    int kind;
    int punct;              /* T_PUNCT: 文字，または下の P_* */
    struct name *id;        /* T_IDENT / T_KW */
    u64 val;                /* T_NUM / T_CHAR */
    int isunsigned;         /* 接尾の u */
    int nlong;              /* 接尾の l の数 */
    int hexoct;             /* 16 進・8 進で書いた */
    char *str;              /* T_STR: 中身 (\0 を含みうる) */
    long slen;
    int wide;
    int line;
    const char *file;
};

/* 2 文字以上の区切り子 */
enum {
    P_ELLIPSIS = 256, P_ARROW, P_INC, P_DEC, P_SHL, P_SHR, P_LE, P_GE, P_EQ, P_NE,
    P_ANDAND, P_OROR, P_MULEQ, P_DIVEQ, P_MODEQ, P_ADDEQ, P_SUBEQ, P_SHLEQ, P_SHREQ,
    P_ANDEQ, P_XOREQ, P_OREQ
};

static struct token *toks;
static long ntok;
static long captok;

static struct token *newtok(int kind)
{
    struct token *t;
    if (ntok >= captok) {
        captok = captok ? captok * 2 : 65536;
        toks = xrealloc(toks, captok * (long)sizeof(struct token));
    }
    t = &toks[ntok++];
    memset(t, 0, sizeof(struct token));
    t->kind = kind;
    t->line = curline;
    t->file = curfile;
    return t;
}

static int isidc(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'
        || (c >= '0' && c <= '9');
}

static int lexesc(const char **pp)
{
    const char *p = *pp;
    int c = *p++;
    if (c != '\\') {
        *pp = p;
        return c & 0xff;
    }
    c = *p++;
    switch (c) {
    case 'n': c = '\n'; break;
    case 't': c = '\t'; break;
    case 'r': c = '\r'; break;
    case 'b': c = '\b'; break;
    case 'f': c = '\f'; break;
    case 'v': c = '\v'; break;
    case 'a': c = 7; break;
    case 'e': case 'E': c = 27; break;
    case 'x': {
        int v = 0;
        for (;;) {
            int d = *p;
            if (d >= '0' && d <= '9') d -= '0';
            else if (d >= 'a' && d <= 'f') d = d - 'a' + 10;
            else if (d >= 'A' && d <= 'F') d = d - 'A' + 10;
            else break;
            v = v * 16 + d;
            p++;
        }
        c = v & 0xff;
        break;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = c - '0', i;
            for (i = 0; i < 2 && *p >= '0' && *p <= '7'; i++)
                v = v * 8 + (*p++ - '0');
            c = v & 0xff;
        }
        break;
    }
    *pp = p;
    return c;
}

/* # <行> "<ファイル>" の行の印 */
static const char *linemarker(const char *p)
{
    const char *q = p + 1;
    while (*q == ' ' || *q == '\t')
        q++;
    if (strncmp(q, "line", 4) == 0 && (q[4] == ' ' || q[4] == '\t')) {
        q += 4;
        while (*q == ' ' || *q == '\t')
            q++;
    }
    if (*q >= '0' && *q <= '9') {
        int n = 0;
        while (*q >= '0' && *q <= '9')
            n = n * 10 + (*q++ - '0');
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '"') {
            const char *e = ++q;
            while (*e && *e != '"' && *e != '\n')
                e++;
            curfile = intern(q, e - q)->s;
        }
        curline = n - 1;    /* 行末の改行で 1 つ進む */
    }
    while (*p && *p != '\n')
        p++;
    return p;
}

static void lex(const char *p)
{
    int bol = 1;
    for (;;) {
        int c = *p;
        if (c == 0 || c == 4) {
            newtok(T_EOF);
            return;
        }
        if (c == '\n') {
            curline++;
            bol = 1;
            p++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            p++;
            continue;
        }
        if (c == '#' && bol) {
            p = linemarker(p);
            continue;
        }
        bol = 0;
        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n')
                    curline++;
                p++;
            }
            if (*p)
                p += 2;
            continue;
        }
        if (c == '/' && p[1] == '/') {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if ((c == 'L' && (p[1] == '"' || p[1] == '\'')) || c == '"' || c == '\'') {
            int wide = c == 'L';
            if (wide)
                p++;
            if (*p == '\'') {
                struct token *t = newtok(T_CHAR);
                u64 v = 0;
                int n = 0;
                p++;
                while (*p && *p != '\'') {
                    int ch = lexesc(&p);
                    /* char は unsigned (psABI)。1 文字なら値そのまま */
                    v = (v << 8) | (u64)(ch & 0xff);
                    n++;
                }
                if (*p != '\'')
                    fatal_at(curfile, curline, "文字定数が閉じていない", 0);
                p++;
                t->val = v;
                t->wide = wide;
                (void)n;
                continue;
            } else {
                struct token *t = newtok(T_STR);
                struct buf b;
                memset(&b, 0, sizeof b);
                p++;
                while (*p && *p != '"') {
                    char ch;
                    if (*p == '\n')
                        fatal_at(curfile, curline, "文字列が閉じていない", 0);
                    ch = (char)lexesc(&p);
                    bput(&b, &ch, 1);
                }
                if (*p != '"')
                    fatal_at(curfile, curline, "文字列が閉じていない", 0);
                p++;
                if (!b.p)
                    bput(&b, "", 0);
                t->str = b.p;
                t->slen = b.n;
                t->wide = wide;
                continue;
            }
        }
        if ((c >= '0' && c <= '9') || (c == '.' && p[1] >= '0' && p[1] <= '9')) {
            const char *s = p;
            struct token *t;
            u64 v = 0;
            int isfloat = 0;
            /* 数の字句は英数字と . と指数の符号までをまとめて読む */
            while (isidc(*p) || *p == '.'
                   || ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E'
                                                    || p[-1] == 'p' || p[-1] == 'P')
                       && !(s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && (p[-1] == 'e' || p[-1] == 'E'))))
                p++;
            {
                const char *q = s;
                int base = 10;
                if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
                    base = 16;
                    q += 2;
                } else if (q[0] == '0' && (q[1] == 'b' || q[1] == 'B')) {
                    base = 2;
                    q += 2;
                } else if (q[0] == '0')
                    base = 8;
                for (; q < p; q++) {
                    int d = *q;
                    if (d >= '0' && d <= '9') d -= '0';
                    else if (base == 16 && d >= 'a' && d <= 'f') d = d - 'a' + 10;
                    else if (base == 16 && d >= 'A' && d <= 'F') d = d - 'A' + 10;
                    else break;
                    if (d >= base) {
                        if (base == 8 && d <= 9) {
                            isfloat = 1;    /* 0 で始まる 10 進の浮動小数点 */
                            break;
                        }
                        break;
                    }
                    v = v * (u64)base + (u64)d;
                }
                if (q < p && (*q == '.' || ((*q == 'e' || *q == 'E') && base != 16)
                              || *q == 'p' || *q == 'P'))
                    isfloat = 1;
                if (isfloat) {
                    t = newtok(T_FNUM);
                    t->str = xstrndup(s, p - s);
                    continue;
                }
                t = newtok(T_NUM);
                t->val = v;
                t->hexoct = base != 10;
                for (; q < p; q++) {
                    if (*q == 'u' || *q == 'U')
                        t->isunsigned = 1;
                    else if (*q == 'l' || *q == 'L')
                        t->nlong++;
                    else
                        fatal_at(curfile, curline, "数の接尾が読めない", xstrndup(s, p - s));
                }
            }
            continue;
        }
        if (isidc(c)) {
            const char *s = p;
            struct name *n;
            struct token *t;
            while (isidc(*p))
                p++;
            n = intern(s, p - s);
            t = newtok(n->kw ? T_KW : T_IDENT);
            t->id = n;
            continue;
        }
        {
            static const char *ps[] = {
                "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
                "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=", 0
            };
            int best = -1, bl = 0, i;
            struct token *t;
            for (i = 0; ps[i]; i++) {
                int l = (int)strlen(ps[i]);
                if (l > bl && strncmp(p, ps[i], l) == 0) {
                    best = i;
                    bl = l;
                }
            }
            t = newtok(T_PUNCT);
            if (best >= 0) {
                t->punct = P_ELLIPSIS + best;
                p += bl;
            } else {
                if (!strchr("{}[]()<>;:,.+-*/%&|^!~?=", c))
                    fatal_at(curfile, curline, "知らない文字", xstrndup(p, 1));
                t->punct = c;
                p++;
            }
        }
    }
}

static void init_keywords(void)
{
    int i;
    for (i = 0; kwdefs[i].name; i++)
        intern(kwdefs[i].name, (long)strlen(kwdefs[i].name))->kw = kwdefs[i].kw;
}

/* ==== 型 ==== */

enum {
    TY_VOID, TY_BOOL, TY_CHAR, TY_SCHAR, TY_UCHAR, TY_SHORT, TY_USHORT, TY_INT, TY_UINT,
    TY_LONG, TY_ULONG, TY_LLONG, TY_ULLONG, TY_FLOAT, TY_DOUBLE, TY_LDOUBLE,
    TY_ENUM, TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT, TY_UNION
};

#define Q_CONST 1
#define Q_VOLATILE 2

struct type;
struct sym;
struct node;

struct member {
    struct name *name;      /* 無名の構造体・共用体の成員なら 0 */
    struct type *ty;
    i64 off;                /* 先頭からのバイト位置 (ビットフィールドは入れ物の位置) */
    int bw;                 /* ビットフィールドの幅 (-1 は普通の成員) */
    int bo;                 /* 入れ物の中のビット位置 */
    struct member *next;
};

/* 構造体・共用体・列挙の定義。修飾した写しと共有する (後から完成する) */
struct sdef {
    struct member *mem;
    i64 size;
    int align;
    int complete;
    int packed;
    struct name *tag;
    struct type *enumbase;  /* 列挙の基底の型 */
};

struct param {
    struct type *ty;
    struct name *name;
    struct param *next;
};

struct type {
    int kind;
    i64 size;
    int align;
    int qual;
    struct type *base;      /* 指すもの・要素・戻り値 */
    i64 len;                /* 配列の長さ (-1 は不完全) */
    struct sdef *sd;
    struct param *params;
    int variadic;
    int proto;              /* 原型がある */
    struct type *ptrto;     /* この型へのポインタ (使い回す) */
};

static struct type ty_void;
static struct type ty_bool;
static struct type ty_char;
static struct type ty_schar;
static struct type ty_uchar;
static struct type ty_short;
static struct type ty_ushort;
static struct type ty_int;
static struct type ty_uint;
static struct type ty_long;
static struct type ty_ulong;
static struct type ty_llong;
static struct type ty_ullong;
static struct type ty_float;
static struct type ty_double;
static struct type ty_ldouble;

/* 基本の型 (実行時に埋める。構造体の初期化子の書き方に依らないため) */
static void init_types(void)
{
    ty_void.kind = TY_VOID;
    ty_void.size = 1;
    ty_void.align = 1;
    ty_bool.kind = TY_BOOL;
    ty_bool.size = 1;
    ty_bool.align = 1;
    ty_char.kind = TY_CHAR;
    ty_char.size = 1;
    ty_char.align = 1;
    ty_schar.kind = TY_SCHAR;
    ty_schar.size = 1;
    ty_schar.align = 1;
    ty_uchar.kind = TY_UCHAR;
    ty_uchar.size = 1;
    ty_uchar.align = 1;
    ty_short.kind = TY_SHORT;
    ty_short.size = 2;
    ty_short.align = 2;
    ty_ushort.kind = TY_USHORT;
    ty_ushort.size = 2;
    ty_ushort.align = 2;
    ty_int.kind = TY_INT;
    ty_int.size = 4;
    ty_int.align = 4;
    ty_uint.kind = TY_UINT;
    ty_uint.size = 4;
    ty_uint.align = 4;
    ty_long.kind = TY_LONG;
    ty_long.size = 4;
    ty_long.align = 4;
    ty_ulong.kind = TY_ULONG;
    ty_ulong.size = 4;
    ty_ulong.align = 4;
    ty_llong.kind = TY_LLONG;
    ty_llong.size = 8;
    ty_llong.align = 8;
    ty_ullong.kind = TY_ULLONG;
    ty_ullong.size = 8;
    ty_ullong.align = 8;
    ty_float.kind = TY_FLOAT;
    ty_float.size = 4;
    ty_float.align = 4;
    ty_double.kind = TY_DOUBLE;
    ty_double.size = 8;
    ty_double.align = 8;
    ty_ldouble.kind = TY_LDOUBLE;
    ty_ldouble.size = 16;
    ty_ldouble.align = 16;
}


static struct type *newtype(int kind, i64 size, int align)
{
    struct type *t = xmalloc(sizeof(struct type));
    t->kind = kind;
    t->size = size;
    t->align = align;
    return t;
}

static struct type *copytype(struct type *t)
{
    struct type *c = xmalloc(sizeof(struct type));
    *c = *t;
    c->ptrto = 0;
    return c;
}

static struct type *qualify(struct type *t, int q)
{
    struct type *c;
    if ((t->qual | q) == t->qual)
        return t;
    c = copytype(t);
    c->qual |= q;
    return c;
}

static struct type *unqual(struct type *t)
{
    struct type *c;
    if (!t->qual)
        return t;
    c = copytype(t);
    c->qual = 0;
    return c;
}

static struct type *ptrto(struct type *t)
{
    if (!t->ptrto) {
        struct type *p = newtype(TY_PTR, 4, 4);
        p->base = t;
        t->ptrto = p;
    }
    return t->ptrto;
}

static struct type *arrayof(struct type *t, i64 len)
{
    struct type *a = newtype(TY_ARRAY, len >= 0 ? len * t->size : 0, t->align);
    a->base = t;
    a->len = len;
    return a;
}

static i64 tsize(struct type *t)
{
    if (t->kind == TY_STRUCT || t->kind == TY_UNION)
        return t->sd->size;
    if (t->kind == TY_ARRAY)
        return t->len >= 0 ? t->len * tsize(t->base) : 0;
    if (t->kind == TY_ENUM)
        return t->sd->enumbase ? t->sd->enumbase->size : 4;
    return t->size;
}

static int talign(struct type *t)
{
    if (t->kind == TY_STRUCT || t->kind == TY_UNION)
        return t->align > t->sd->align ? t->align : t->sd->align;
    if (t->kind == TY_ARRAY)
        return t->align > talign(t->base) ? t->align : talign(t->base);
    if (t->kind == TY_ENUM)
        return t->sd->enumbase ? t->sd->enumbase->align : 4;
    return t->align;
}

static int isint(struct type *t)
{
    return (t->kind >= TY_BOOL && t->kind <= TY_ULLONG) || t->kind == TY_ENUM;
}

static int isflt(struct type *t)
{
    return t->kind >= TY_FLOAT && t->kind <= TY_LDOUBLE;
}

static int isarith(struct type *t)
{
    return isint(t) || isflt(t);
}

static int isptr(struct type *t)
{
    return t->kind == TY_PTR;
}

static int isscalar(struct type *t)
{
    return isarith(t) || isptr(t);
}

static int isaggr(struct type *t)
{
    return t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_ARRAY;
}

static int is64(struct type *t)
{
    return t->kind == TY_LLONG || t->kind == TY_ULLONG
        || (t->kind == TY_ENUM && t->sd->enumbase && t->sd->enumbase->size == 8);
}

static int isunsignedt(struct type *t)
{
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_UCHAR: case TY_USHORT: case TY_UINT:
    case TY_ULONG: case TY_ULLONG: case TY_PTR:
        return 1;
    case TY_ENUM:
        return t->sd->enumbase && isunsignedt(t->sd->enumbase);
    }
    return 0;
}

/* 整数の順位 (整数の昇格と通常の算術変換に使う) */
static int rank(struct type *t)
{
    switch (t->kind) {
    case TY_BOOL: return 0;
    case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 1;
    case TY_SHORT: case TY_USHORT: return 2;
    case TY_INT: case TY_UINT: return 3;
    case TY_LONG: case TY_ULONG: return 4;
    case TY_LLONG: case TY_ULLONG: return 5;
    case TY_ENUM: return t->sd->enumbase ? rank(t->sd->enumbase) : 3;
    }
    return 3;
}

/* 型の互換 (修飾は除く) */
static int compatible(struct type *a, struct type *b)
{
    if (a == b)
        return 1;
    if (a->kind == TY_ENUM && b->kind != TY_ENUM)
        return compatible(a->sd->enumbase ? a->sd->enumbase : &ty_uint, b);
    if (b->kind == TY_ENUM && a->kind != TY_ENUM)
        return compatible(a, b->sd->enumbase ? b->sd->enumbase : &ty_uint);
    if (a->kind != b->kind)
        return 0;
    switch (a->kind) {
    case TY_PTR:
        return a->base->qual == b->base->qual && compatible(a->base, b->base);
    case TY_ARRAY:
        if (a->len >= 0 && b->len >= 0 && a->len != b->len)
            return 0;
        return compatible(a->base, b->base);
    case TY_STRUCT: case TY_UNION: case TY_ENUM:
        return a->sd == b->sd;
    case TY_FUNC: {
        struct param *p, *q;
        if (!compatible(a->base, b->base))
            return 0;
        if (!a->proto || !b->proto)
            return 1;
        if (a->variadic != b->variadic)
            return 0;
        for (p = a->params, q = b->params; p && q; p = p->next, q = q->next)
            if (!compatible(unqual(p->ty), unqual(q->ty)))
                return 0;
        return !p && !q;
    }
    }
    return 1;
}

/* ==== 記号 ==== */

enum { SY_VAR, SY_FUNC, SY_TYPEDEF, SY_ENUMC, SY_TAG };
enum { SC_NONE, SC_STATIC, SC_EXTERN, SC_AUTO, SC_REGISTER, SC_TYPEDEF };

/* 属性 (__attribute__) のうち生成に効くもの (docs/stage018-ncc.md 4.8) */
struct attrs {
    int aligned;
    int packed;
    char *section;
    int weak;
    char *alias;
    int used;
    int unused;
    int noreturn;
    int gnu_inline;
    int always_inline;
    char *error;
    int mode;               /* 0 なし，1 QI，2 HI，4 SI，8 DI */
    char *asmlabel;         /* asm("名前") */
    char *regname;          /* register T x asm("a0") */
    int hidden;
    int transparent_union;
};

struct sym {
    struct name *name;
    int kind;
    struct type *ty;
    int sclass;
    int global;             /* ファイルの有効範囲 */
    int level;
    i64 val;                /* SY_ENUMC の値 */
    i64 off;                /* 局所の変数: s0 からの位置 (負) */
    char *label;            /* アセンブリの名前 */
    int defined;            /* 関数の本体，または初期化子のある定義 */
    int tentative;
    int referenced;
    int emitted;
    int isinline;
    struct node *body;      /* 関数の本体 */
    struct ient *init;      /* 静的な初期化子 */
    int hasinit;
    struct attrs at;
    struct sym *shadow;     /* 同じ名前の外側の束縛 */
    struct sym *scopenext;  /* 同じ有効範囲の記号の並び */
    struct sym *gnext;      /* ファイルの有効範囲の記号の並び (出力の順) */
    struct sym *locals;     /* 関数の局所の変数の並び */
    struct sym *lnext;
    struct sym *params;     /* 関数の仮引数 (記号) */
    int isparam;
    int addrtaken;
    int isstaticlocal;
    struct sym *fnext;      /* 出力を待つ static の並び */
    struct sym *alias_of;   /* 局所の extern 宣言: ファイルの有効範囲の記号 */
    struct sym *pnext;      /* 仮引数の並び */
    int byref;              /* 番地で渡された構造体の仮引数 (置き場はポインタ) */
    const char *file;
    int line;
};

/* 有効範囲 */
struct scope {
    struct sym *syms;
    struct scope *up;
};

static struct scope *curscope;
static int level;
static struct sym *globals;
static struct sym **gtail = &globals;

static void push_scope(void)
{
    struct scope *s = xmalloc(sizeof(struct scope));
    s->up = curscope;
    curscope = s;
    level++;
}

static void pop_scope(void)
{
    struct sym *s;
    for (s = curscope->syms; s; s = s->scopenext) {
        if (s->kind == SY_TAG)
            s->name->tag = s->shadow;
        else
            s->name->ord = s->shadow;
    }
    curscope = curscope->up;
    level--;
}

static struct sym *bind(struct name *n, int kind, struct type *ty)
{
    struct sym *s = xmalloc(sizeof(struct sym));
    s->name = n;
    s->kind = kind;
    s->ty = ty;
    s->level = level;
    s->global = level == 0;
    s->file = curfile;
    s->line = curline;
    if (n) {
        if (kind == SY_TAG) {
            s->shadow = n->tag;
            n->tag = s;
        } else {
            s->shadow = n->ord;
            n->ord = s;
        }
        s->scopenext = curscope->syms;
        curscope->syms = s;
    }
    return s;
}

/* ==== 構文木 ==== */

enum {
    /* 式 */
    N_NUM = 1, N_STR, N_VAR, N_ADD, N_SUB, N_MUL, N_DIV, N_MOD, N_SHL, N_SHR, N_AND,
    N_OR, N_XOR, N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE, N_LAND, N_LOR, N_NOT, N_BNOT,
    N_NEG, N_ASSIGN, N_OPASSIGN, N_PREINC, N_POSTINC, N_ADDR, N_DEREF, N_MEMBER,
    N_CALL, N_CAST, N_COND, N_COMMA, N_PSUB, N_STMTEXPR, N_VASTART, N_VAARG, N_VAEND,
    N_VACOPY, N_LABELADDR, N_BUILTIN, N_INITLIST,
    /* 文 */
    S_BLOCK = 100, S_EXPR, S_IF, S_WHILE, S_DO, S_FOR, S_SWITCH, S_CASE, S_DEFAULT,
    S_BREAK, S_CONTINUE, S_RETURN, S_GOTO, S_LABEL, S_NULL, S_INIT, S_ASM
};

/* 文字列定数 */
struct strlit {
    char *s;
    long len;               /* 終端を除く */
    int id;
    int wide;
    int used;
    struct strlit *next;
};

/* 初期化子を平らにしたもの (位置・型・値) */
struct ient {
    i64 off;
    struct type *ty;
    struct node *expr;
    struct member *bf;      /* ビットフィールドなら成員 */
    struct ient *next;
};

struct node {
    int op;
    struct type *ty;
    struct node *a;
    struct node *b;
    struct node *c;
    struct node *d;
    struct node *next;
    i64 val;
    i64 val2;
    struct sym *sym;
    struct member *mem;
    struct strlit *str;
    struct type *ctype;     /* 複合代入の計算の型 */
    struct ient *init;      /* S_INIT / N_INITLIST */
    struct node *cases;     /* S_SWITCH: 場合の並び */
    struct node *nextcase;
    int label;              /* 生成で使うラベルの番号 */
    int targeted;           /* S_CASE / S_DEFAULT: switch から跳んでくる */
    struct name *lname;     /* S_GOTO / S_LABEL */
    char *asmtext;          /* S_ASM */
    const char *file;
    int line;
};

static struct node *mknode(int op, struct type *ty)
{
    struct node *n = xmalloc(sizeof(struct node));
    n->op = op;
    n->ty = ty;
    n->file = curfile;
    n->line = curline;
    return n;
}

/* ==== 構文解析: 字句の操作 ==== */

static long tp;

static struct token *tok(void)
{
    return &toks[tp];
}

static struct token *peekn(int k)
{
    long i = tp + k;
    if (i >= ntok)
        i = ntok - 1;
    return &toks[i];
}

static void error_tok(struct token *t, const char *msg, const char *arg)
{
    fatal_at(t->file, t->line, msg, arg);
}

static void perr(const char *msg)
{
    struct token *t = tok();
    const char *what = t->kind == T_IDENT || t->kind == T_KW ? t->id->s
        : t->kind == T_EOF ? "(入力の終わり)" : t->kind == T_PUNCT ? "(区切り子)"
        : t->kind == T_STR ? "(文字列)" : "(数)";
    error_tok(t, msg, what);
}

static void advance(void)
{
    if (toks[tp].kind != T_EOF)
        tp++;
    curfile = toks[tp].file;
    curline = toks[tp].line;
}

static int ispunct_(int c)
{
    return tok()->kind == T_PUNCT && tok()->punct == c;
}

static int iskw(int k)
{
    return tok()->kind == T_KW && tok()->id->kw == k;
}

static int accept_p(int c)
{
    if (ispunct_(c)) {
        advance();
        return 1;
    }
    return 0;
}

static int accept_kw(int k)
{
    if (iskw(k)) {
        advance();
        return 1;
    }
    return 0;
}

static void expect_p(int c)
{
    if (!accept_p(c)) {
        char m[48];
        if (c < 256)
            sprintf(m, "'%c' が要る", c);
        else
            sprintf(m, "区切り子 %d が要る", c);
        perr(m);
    }
}

static struct name *expect_ident(void)
{
    struct name *n;
    if (tok()->kind != T_IDENT)
        perr("識別子が要る");
    n = tok()->id;
    advance();
    return n;
}

/* ==== 構文解析: 前方宣言 ==== */

static struct node *expr(void);
static struct node *assign_expr(void);
static struct node *cond_expr(void);
static struct type *typename_(void);
static int is_typename(struct token *t);
static i64 const_expr(void);
static struct node *conv(struct node *n, struct type *ty);
static int eval_int(struct node *n, i64 *v);
static void parse_attrs(struct attrs *a);
static struct node *compound_stmt(int newscope);
static void declaration(int global, struct node ***tail);
static struct ient *parse_initializer(struct type **ty, int isstatic);
static struct type *declarator(struct type *base, struct name **name);
static void static_assert_decl(void);

/* ==== 属性 ==== */

static int attrname_is(struct name *n, const char *s)
{
    long l = (long)strlen(s);
    const char *p = n->s;
    long pl = n->len;
    if (pl > 4 && p[0] == '_' && p[1] == '_' && p[pl - 1] == '_' && p[pl - 2] == '_') {
        p += 2;
        pl -= 4;
    }
    return pl == l && memcmp(p, s, l) == 0;
}

static char *string_arg(void)
{
    struct buf b;
    memset(&b, 0, sizeof b);
    if (tok()->kind != T_STR)
        perr("文字列が要る");
    while (tok()->kind == T_STR) {
        bput(&b, tok()->str, tok()->slen);
        advance();
    }
    if (!b.p)
        bput(&b, "", 0);
    return b.p;
}

static void skip_balanced(void)
{
    int depth = 0;
    for (;;) {
        if (tok()->kind == T_EOF)
            perr("括弧が閉じていない");
        if (ispunct_('('))
            depth++;
        else if (ispunct_(')')) {
            if (depth == 0)
                return;
            depth--;
        }
        advance();
    }
}

static void one_attr(struct attrs *a)
{
    struct name *n;
    if (tok()->kind != T_IDENT && tok()->kind != T_KW)
        perr("属性の名前が要る");
    n = tok()->id;
    advance();
    if (attrname_is(n, "aligned")) {
        a->aligned = 16;    /* __BIGGEST_ALIGNMENT__ */
        if (accept_p('(')) {
            a->aligned = (int)const_expr();
            expect_p(')');
        }
        return;
    }
    if (attrname_is(n, "packed")) { a->packed = 1; goto skipargs; }
    if (attrname_is(n, "weak")) { a->weak = 1; goto skipargs; }
    if (attrname_is(n, "used")) { a->used = 1; goto skipargs; }
    if (attrname_is(n, "unused")) { a->unused = 1; goto skipargs; }
    if (attrname_is(n, "noreturn")) { a->noreturn = 1; goto skipargs; }
    if (attrname_is(n, "gnu_inline")) { a->gnu_inline = 1; goto skipargs; }
    if (attrname_is(n, "always_inline")) { a->always_inline = 1; goto skipargs; }
    if (attrname_is(n, "transparent_union")) { a->transparent_union = 1; goto skipargs; }
    if (attrname_is(n, "section") || attrname_is(n, "alias") || attrname_is(n, "error")
        || attrname_is(n, "visibility")) {
        char *s;
        expect_p('(');
        s = string_arg();
        expect_p(')');
        if (attrname_is(n, "section"))
            a->section = s;
        else if (attrname_is(n, "alias"))
            a->alias = s;
        else if (attrname_is(n, "error"))
            a->error = s;
        else if (strcmp(s, "hidden") == 0)
            a->hidden = 1;
        return;
    }
    if (attrname_is(n, "mode")) {
        struct name *m;
        expect_p('(');
        m = tok()->id;
        if (!m)
            perr("mode の引数が要る");
        advance();
        expect_p(')');
        if (attrname_is(m, "QI") || attrname_is(m, "byte"))
            a->mode = 1;
        else if (attrname_is(m, "HI"))
            a->mode = 2;
        else if (attrname_is(m, "SI") || attrname_is(m, "word") || attrname_is(m, "pointer"))
            a->mode = 4;
        else if (attrname_is(m, "DI"))
            a->mode = 8;
        else
            error_tok(tok(), "扱わない mode", m->s);
        return;
    }
skipargs:
    /* 生成に効かない属性 (format・nonnull・cold など) は引数ごと読み捨てる */
    if (accept_p('(')) {
        skip_balanced();
        expect_p(')');
    }
}

/* __attribute__((...)) と asm("名前") の並び */
static void parse_attrs(struct attrs *a)
{
    for (;;) {
        if (accept_kw(K_ATTRIBUTE)) {
            expect_p('(');
            expect_p('(');
            while (!ispunct_(')')) {
                if (accept_p(','))
                    continue;
                one_attr(a);
            }
            expect_p(')');
            expect_p(')');
            continue;
        }
        if (iskw(K_ASM)) {
            advance();
            expect_p('(');
            a->asmlabel = string_arg();
            expect_p(')');
            continue;
        }
        break;
    }
}

/* ==== 宣言指定子 ==== */

struct dspec {
    int sclass;
    struct type *ty;
    int isinline;
    struct attrs at;
};

static struct type *struct_decl(int kind);
static struct type *enum_decl(void);

static int typedef_name(struct token *t)
{
    return t->kind == T_IDENT && t->id->ord && t->id->ord->kind == SY_TYPEDEF;
}

static int is_typename(struct token *t)
{
    if (t->kind == T_KW) {
        switch (t->id->kw) {
        case K_CHAR: case K_CONST: case K_DOUBLE: case K_ENUM: case K_FLOAT:
        case K_INT: case K_LONG: case K_SHORT: case K_SIGNED: case K_STRUCT:
        case K_UNION: case K_UNSIGNED: case K_VOID: case K_VOLATILE: case K_TYPEOF:
        case K_BOOL: case K_RESTRICT: case K_ATTRIBUTE: case K_EXTENSION: case K_VA_LIST:
            return 1;
        }
        return 0;
    }
    return typedef_name(t);
}

static int is_declstart(struct token *t)
{
    if (t->kind == T_KW) {
        switch (t->id->kw) {
        case K_AUTO: case K_EXTERN: case K_REGISTER: case K_STATIC: case K_TYPEDEF:
        case K_INLINE: case K_STATIC_ASSERT:
            return 1;
        }
    }
    return is_typename(t);
}

/* 型を mode 属性の幅に替える */
static struct type *apply_mode(struct type *t, int mode)
{
    int u = isunsignedt(t);
    switch (mode) {
    case 1: return u ? &ty_uchar : &ty_schar;
    case 2: return u ? &ty_ushort : &ty_short;
    case 4: return u ? &ty_uint : &ty_int;
    case 8: return u ? &ty_ullong : &ty_llong;
    }
    return t;
}

static void declspec(struct dspec *d, int allow_sclass)
{
    int nvoid = 0, nchar = 0, nshort = 0, nint = 0, nlong = 0, nsigned = 0, nunsigned = 0;
    int nbool = 0, nfloat = 0, ndouble = 0, qual = 0;
    struct type *other = 0;
    memset(d, 0, sizeof(struct dspec));
    for (;;) {
        struct token *t = tok();
        if (t->kind == T_KW) {
            switch (t->id->kw) {
            case K_TYPEDEF: case K_STATIC: case K_EXTERN: case K_AUTO: case K_REGISTER:
                if (!allow_sclass)
                    perr("ここに記憶域の指定は書けない");
                if (d->sclass)
                    perr("記憶域の指定が重なった");
                d->sclass = t->id->kw == K_TYPEDEF ? SC_TYPEDEF : t->id->kw == K_STATIC ? SC_STATIC
                    : t->id->kw == K_EXTERN ? SC_EXTERN : t->id->kw == K_AUTO ? SC_AUTO : SC_REGISTER;
                advance();
                continue;
            case K_INLINE: d->isinline = 1; advance(); continue;
            case K_CONST: qual |= Q_CONST; advance(); continue;
            case K_VOLATILE: qual |= Q_VOLATILE; advance(); continue;
            case K_RESTRICT: case K_EXTENSION: advance(); continue;
            case K_ATTRIBUTE: parse_attrs(&d->at); continue;
            case K_VOID: nvoid++; advance(); continue;
            case K_CHAR: nchar++; advance(); continue;
            case K_SHORT: nshort++; advance(); continue;
            case K_INT: nint++; advance(); continue;
            case K_LONG: nlong++; advance(); continue;
            case K_SIGNED: nsigned++; advance(); continue;
            case K_UNSIGNED: nunsigned++; advance(); continue;
            case K_BOOL: nbool++; advance(); continue;
            case K_FLOAT: nfloat++; advance(); continue;
            case K_DOUBLE: ndouble++; advance(); continue;
            case K_VA_LIST:
                if (other) perr("型の指定が重なった");
                other = ptrto(&ty_void);
                advance();
                continue;
            case K_STRUCT: case K_UNION:
                if (other) perr("型の指定が重なった");
                advance();
                other = struct_decl(t->id->kw == K_STRUCT ? TY_STRUCT : TY_UNION);
                continue;
            case K_ENUM:
                if (other) perr("型の指定が重なった");
                advance();
                other = enum_decl();
                continue;
            case K_TYPEOF:
                if (other) perr("型の指定が重なった");
                advance();
                expect_p('(');
                if (is_typename(tok()))
                    other = typename_();
                else {
                    struct node *e = expr();
                    other = e->ty;
                }
                expect_p(')');
                continue;
            }
            break;
        }
        if (typedef_name(t) && !other && !nvoid && !nchar && !nshort && !nint && !nlong
            && !nsigned && !nunsigned && !nbool && !nfloat && !ndouble) {
            other = t->id->ord->ty;
            advance();
            continue;
        }
        break;
    }
    if (other) {
        if (nvoid || nchar || nshort || nint || nlong || nsigned || nunsigned || nbool
            || nfloat || ndouble)
            perr("型の指定が重なった");
        d->ty = other;
    } else if (nvoid)
        d->ty = &ty_void;
    else if (nbool)
        d->ty = &ty_bool;
    else if (nchar)
        d->ty = nunsigned ? &ty_uchar : nsigned ? &ty_schar : &ty_char;
    else if (nfloat)
        d->ty = &ty_float;
    else if (ndouble)
        d->ty = nlong ? &ty_ldouble : &ty_double;
    else if (nshort)
        d->ty = nunsigned ? &ty_ushort : &ty_short;
    else if (nlong >= 2)
        d->ty = nunsigned ? &ty_ullong : &ty_llong;
    else if (nlong == 1)
        d->ty = nunsigned ? &ty_ulong : &ty_long;
    else if (nint || nsigned || nunsigned)
        d->ty = nunsigned ? &ty_uint : &ty_int;
    else
        d->ty = 0;          /* 型の無い宣言 (暗黙の int は呼ぶ側が判断する) */
    if (d->ty && d->at.mode)
        d->ty = apply_mode(d->ty, d->at.mode);
    if (d->ty && qual)
        d->ty = qualify(d->ty, qual);
    if (!d->ty && qual)
        d->ty = qualify(&ty_int, qual);
}

/* ==== 構造体・共用体 ==== */

static struct member *find_member(struct type *t, struct name *n, i64 *off);

/* 成員を並べる (psABI と GCC のビットフィールドの規則) */
static void layout_struct(struct sdef *sd, int kind)
{
    struct member *m;
    i64 bitpos = 0, size = 0;
    int align = 1;
    for (m = sd->mem; m; m = m->next) {
        struct type *t = m->ty;
        i64 tsz = tsize(t);
        int ta = sd->packed ? 1 : talign(t);
        if (m->bw >= 0) {
            int unit = talign(t) * 8;
            if (m->bw == 0) {
                /* 幅 0 は次の入れ物の境界まで進める */
                if (kind == TY_STRUCT && unit > 0)
                    bitpos = (bitpos + unit - 1) / unit * unit;
                m->off = bitpos / 8;
                m->bo = 0;
                continue;
            }
            if (kind == TY_UNION)
                bitpos = 0;
            if (!sd->packed && (bitpos / unit) != ((bitpos + m->bw - 1) / unit))
                bitpos = (bitpos + unit - 1) / unit * unit;
            /* 入れ物は宣言の型の大きさで，その整列の境界に置く */
            m->off = (bitpos / (tsz * 8)) * tsz;
            m->bo = (int)(bitpos - m->off * 8);
            if (m->bo + m->bw > tsz * 8) {
                /* packed で入れ物をまたぐもの。バイト単位の位置に置き直す */
                m->off = bitpos / 8;
                m->bo = (int)(bitpos % 8);
            }
            if (m->name && ta > align)
                align = ta;
            if (kind == TY_UNION) {
                if ((m->bw + 7) / 8 > size)
                    size = (m->bw + 7) / 8;
            } else
                bitpos += m->bw;
            continue;
        }
        if (kind == TY_UNION) {
            m->off = 0;
            if (tsz > size)
                size = tsz;
        } else {
            i64 bytepos = (bitpos + 7) / 8;
            bytepos = (bytepos + ta - 1) / ta * ta;
            m->off = bytepos;
            bitpos = (bytepos + tsz) * 8;
        }
        if (ta > align)
            align = ta;
    }
    if (kind == TY_STRUCT)
        size = (bitpos + 7) / 8;
    if (sd->align > align)
        align = sd->align;
    sd->align = align;
    sd->size = (size + align - 1) / align * align;
    sd->complete = 1;
}

static struct type *struct_decl(int kind)
{
    struct attrs at;
    struct name *tag = 0;
    struct sym *s = 0;
    struct type *t;
    memset(&at, 0, sizeof at);
    parse_attrs(&at);
    if (tok()->kind == T_IDENT) {
        tag = tok()->id;
        advance();
    }
    parse_attrs(&at);
    if (tag && !ispunct_('{')) {
        /* 参照，または前方宣言 (struct S;) */
        int fwd = ispunct_(';');
        s = tag->tag;
        if (s && (!fwd || s->level == level)) {
            if (s->ty->kind != kind)
                perr("タグの種類が違う");
            return s->ty;
        }
        t = newtype(kind, 0, 1);
        t->sd = xmalloc(sizeof(struct sdef));
        t->sd->tag = tag;
        t->sd->align = 1;
        bind(tag, SY_TAG, t);
        return t;
    }
    expect_p('{');
    if (tag && tag->tag && tag->tag->level == level && !tag->tag->ty->sd->complete
        && tag->tag->ty->kind == kind)
        t = tag->tag->ty;
    else {
        t = newtype(kind, 0, 1);
        t->sd = xmalloc(sizeof(struct sdef));
        t->sd->tag = tag;
        t->sd->align = 1;
        if (tag)
            bind(tag, SY_TAG, t);
    }
    {
        struct member head, *tail = &head;
        head.next = 0;
        while (!accept_p('}')) {
            struct dspec d;
            if (accept_p(';'))
                continue;
            if (iskw(K_STATIC_ASSERT)) {
                static_assert_decl();
                continue;
            }
            declspec(&d, 0);
            if (!d.ty)
                perr("成員の型が要る");
            if (ispunct_(';') && (d.ty->kind == TY_STRUCT || d.ty->kind == TY_UNION)) {
                /* 無名の構造体・共用体の成員 */
                struct member *m = xmalloc(sizeof(struct member));
                m->ty = d.ty;
                m->bw = -1;
                tail->next = m;
                tail = m;
                advance();
                continue;
            }
            for (;;) {
                struct member *m = xmalloc(sizeof(struct member));
                struct name *mn = 0;
                struct type *mt = d.ty;
                struct attrs ma;
                memset(&ma, 0, sizeof ma);
                m->bw = -1;
                if (!ispunct_(':'))
                    mt = declarator(d.ty, &mn);
                if (accept_p(':')) {
                    i64 w = const_expr();
                    if (w < 0 || w > tsize(mt) * 8)
                        perr("ビットフィールドの幅が範囲外");
                    m->bw = (int)w;
                }
                parse_attrs(&ma);
                if (ma.mode)
                    mt = apply_mode(mt, ma.mode);
                if (ma.aligned || d.at.aligned) {
                    int al = ma.aligned > d.at.aligned ? ma.aligned : d.at.aligned;
                    if (al > talign(mt)) {
                        mt = copytype(mt);
                        mt->align = al;
                    }
                }
                if (ma.packed && talign(mt) > 1 && m->bw < 0) {
                    mt = copytype(mt);
                    mt->align = 1;
                    if (mt->kind == TY_STRUCT || mt->kind == TY_UNION) {
                        /* 成員に付けた packed は成員の整列を 1 にする */
                    }
                }
                m->name = mn;
                m->ty = mt;
                tail->next = m;
                tail = m;
                if (!accept_p(','))
                    break;
            }
            expect_p(';');
        }
        t->sd->mem = head.next;
    }
    parse_attrs(&at);
    if (at.packed)
        t->sd->packed = 1;
    if (at.aligned > t->sd->align)
        t->sd->align = at.aligned;
    if (at.transparent_union)
        t->sd->packed |= 2;
    layout_struct(t->sd, kind);
    return t;
}

/* 成員を名前で引く。無名の成員の中も探し，位置を足して返す */
static struct member *find_member(struct type *t, struct name *n, i64 *off)
{
    struct member *m;
    for (m = t->sd->mem; m; m = m->next) {
        if (m->name == n) {
            *off = m->off;
            return m;
        }
        if (!m->name && (m->ty->kind == TY_STRUCT || m->ty->kind == TY_UNION)) {
            i64 o;
            struct member *r = find_member(m->ty, n, &o);
            if (r) {
                *off = m->off + o;
                return r;
            }
        }
    }
    return 0;
}

/* ==== 列挙 ==== */

static struct type *enum_decl(void)
{
    struct attrs at;
    struct name *tag = 0;
    struct type *t;
    i64 v = 0, lo = 0, hi = 0;
    struct sym *first = 0, *s;
    int n = 0;
    memset(&at, 0, sizeof at);
    parse_attrs(&at);
    if (tok()->kind == T_IDENT) {
        tag = tok()->id;
        advance();
    }
    if (!ispunct_('{')) {
        if (tag && tag->tag)
            return tag->tag->ty;
        /* 前方の列挙 (GCC の拡張)。基底は unsigned int とする */
        t = &ty_uint;
        if (tag)
            bind(tag, SY_TAG, t);
        return t;
    }
    advance();
    while (!accept_p('}')) {
        struct name *en = expect_ident();
        struct attrs ea;
        memset(&ea, 0, sizeof ea);
        parse_attrs(&ea);
        if (accept_p('='))
            v = const_expr();
        s = bind(en, SY_ENUMC, &ty_int);
        s->val = v;
        if (!first)
            first = s;
        if (n == 0 || v < lo)
            lo = v;
        if (n == 0 || v > hi)
            hi = v;
        n++;
        v++;
        if (!accept_p(','))
            {
                expect_p('}');
                break;
            }
    }
    parse_attrs(&at);
    /* 基底の型 (GCC): 負が無ければ unsigned int，あれば int。32 bit に
     * 収まらなければ long long / unsigned long long */
    if (lo >= 0)
        t = hi <= 0xffffffffLL ? &ty_uint : &ty_ullong;
    else
        t = (lo >= -2147483647LL - 1 && hi <= 2147483647LL) ? &ty_int : &ty_llong;
    if (at.packed) {
        if (lo >= 0)
            t = hi <= 0xff ? &ty_uchar : hi <= 0xffff ? &ty_ushort : t;
        else
            t = (lo >= -128 && hi <= 127) ? &ty_schar : (lo >= -32768 && hi <= 32767) ? &ty_short : t;
    }
    /* 定数の型: int に収まれば int，でなければ列挙の型 */
    for (s = curscope->syms; s; s = s->scopenext) {
        if (s->kind != SY_ENUMC)
            continue;
        if (s->val < -2147483647LL - 1 || s->val > 2147483647LL)
            s->ty = t;
        if (s == first)
            break;
    }
    if (tag)
        bind(tag, SY_TAG, t);
    return t;
}

/* ==== 宣言子 ==== */

static struct param *param_list(int *variadic, int *proto);

/* 宣言子の後ろの [] と () */
static struct type *suffixes(struct type *base)
{
    if (accept_p('[')) {
        i64 len = -1;
        struct type *t;
        while (accept_kw(K_STATIC) || accept_kw(K_CONST) || accept_kw(K_VOLATILE)
               || accept_kw(K_RESTRICT))
            ;
        if (!ispunct_(']')) {
            struct node *e = assign_expr();
            if (!eval_int(e, &len))
                perr("配列の長さが定数でない (可変長の配列は持たない)");
            if (len < 0)
                perr("配列の長さが負");
        }
        expect_p(']');
        base = suffixes(base);
        if (base->kind == TY_FUNC)
            perr("関数の配列は書けない");
        t = arrayof(base, len);
        return t;
    }
    if (accept_p('(')) {
        struct type *t = newtype(TY_FUNC, 1, 1);
        t->params = param_list(&t->variadic, &t->proto);
        base = suffixes(base);
        if (base->kind == TY_ARRAY || base->kind == TY_FUNC)
            perr("配列や関数を返す関数は書けない");
        t->base = base;
        return t;
    }
    return base;
}

static struct type *declarator(struct type *base, struct name **name)
{
    struct attrs dummy;
    memset(&dummy, 0, sizeof dummy);
    *name = 0;
    while (accept_p('*')) {
        int q = 0;
        base = ptrto(base);
        for (;;) {
            if (accept_kw(K_CONST))
                q |= Q_CONST;
            else if (accept_kw(K_VOLATILE))
                q |= Q_VOLATILE;
            else if (accept_kw(K_RESTRICT))
                ;
            else if (iskw(K_ATTRIBUTE))
                parse_attrs(&dummy);
            else
                break;
        }
        base = qualify(base, q);
    }
    parse_attrs(&dummy);
    /* ( が入れ子の宣言子か，引数の並びか。型名や ) が続けば引数の並び */
    if (ispunct_('(') && !is_typename(peekn(1)) && !(peekn(1)->kind == T_PUNCT
                                                    && peekn(1)->punct == ')')) {
        struct type *ph, *inner;
        advance();
        ph = newtype(TY_VOID, 1, 1);
        inner = declarator(ph, name);
        expect_p(')');
        base = suffixes(base);
        /* 仮の型を外側の型で置き換える (内側の鎖は ph を指している) */
        *ph = *base;
        ph->ptrto = 0;
        return inner;
    }
    if (tok()->kind == T_IDENT) {
        *name = tok()->id;
        advance();
    }
    return suffixes(base);
}

/* 仮引数の並び。( は読んだ後 */
static struct param *param_list(int *variadic, int *proto)
{
    struct param head, *tail = &head;
    head.next = 0;
    *variadic = 0;
    *proto = 1;
    if (accept_p(')')) {
        *proto = 0;         /* f() は原型を持たない */
        return 0;
    }
    if (iskw(K_VOID) && peekn(1)->kind == T_PUNCT && peekn(1)->punct == ')') {
        advance();
        advance();
        return 0;
    }
    if (tok()->kind == T_IDENT && !typedef_name(tok())) {
        /* K&R の名前の並びは持たない */
        perr("原型でない関数の定義 (K&R の形) は扱わない");
    }
    for (;;) {
        struct dspec d;
        struct param *p;
        struct name *pn;
        struct type *t;
        if (accept_p(P_ELLIPSIS)) {
            *variadic = 1;
            break;
        }
        declspec(&d, 1);
        if (!d.ty)
            d.ty = &ty_int;
        t = declarator(d.ty, &pn);
        parse_attrs(&d.at);
        if (d.at.mode)
            t = apply_mode(t, d.at.mode);
        /* 仮引数の型の調整: 配列はポインタに，関数は関数へのポインタに */
        if (t->kind == TY_ARRAY)
            t = qualify(ptrto(t->base), t->qual);
        else if (t->kind == TY_FUNC)
            t = ptrto(t);
        p = xmalloc(sizeof(struct param));
        p->ty = t;
        p->name = pn;
        tail->next = p;
        tail = p;
        if (!accept_p(','))
            break;
    }
    expect_p(')');
    return head.next;
}

/* 型名 (キャスト・sizeof・typeof) */
static struct type *typename_(void)
{
    struct dspec d;
    struct name *n;
    struct type *t;
    declspec(&d, 0);
    if (!d.ty)
        perr("型名が要る");
    t = declarator(d.ty, &n);
    if (n)
        perr("型名に名前がある");
    return t;
}

/* ==== 式: 型の変換と定数の畳込み ==== */

static struct sym *curfn;           /* 解析中の関数 */
static struct strlit *strlits;
static struct strlit **strtail = &strlits;
static int nstrlit;

/* v を型 t の値に切り詰める (整数の型のみ) */
static i64 wrap(i64 v, struct type *t)
{
    if (t->kind == TY_BOOL)
        return v != 0;
    if (isptr(t))
        return (i64)((u64)v & 0xffffffffULL);
    switch (tsize(t)) {
    case 1:
        return isunsignedt(t) ? (i64)((u64)v & 0xff) : (i64)(signed char)(v & 0xff);
    case 2:
        return isunsignedt(t) ? (i64)((u64)v & 0xffff)
            : (i64)((((u64)v & 0xffff) ^ 0x8000) - 0x8000);
    case 4:
        return isunsignedt(t) ? (i64)((u64)v & 0xffffffffULL)
            : (i64)((((u64)v & 0xffffffffULL) ^ 0x80000000ULL) - 0x80000000ULL);
    }
    return v;
}

static struct node *mknum(i64 v, struct type *t)
{
    struct node *n = mknode(N_NUM, t);
    n->val = wrap(v, t);
    return n;
}

static int eval_int(struct node *n, i64 *v)
{
    if (n->op == N_NUM && (isint(n->ty) || isptr(n->ty))) {
        *v = n->val;
        return 1;
    }
    return 0;
}

static i64 const_expr(void)
{
    struct node *n = cond_expr();
    i64 v;
    if (!eval_int(n, &v))
        error_tok(tok(), "整数定数式でない", 0);
    return v;
}

static int is_lvalue(struct node *n)
{
    return n->op == N_VAR || n->op == N_DEREF || n->op == N_MEMBER || n->op == N_STR
        || (n->op == N_CAST && n->val2 == 1 && is_lvalue(n->a));
}

/* 配列はポインタに，関数は関数へのポインタに */
static struct node *decay(struct node *n)
{
    struct node *a;
    if (n->ty->kind == TY_ARRAY) {
        a = mknode(N_ADDR, ptrto(n->ty->base));
        a->a = n;
        return a;
    }
    if (n->ty->kind == TY_FUNC) {
        a = mknode(N_ADDR, ptrto(n->ty));
        a->a = n;
        return a;
    }
    return n;
}

/* 型 ty へ変換する。定数はその場で畳み込む */
static struct node *conv(struct node *n, struct type *ty)
{
    struct node *c;
    n = decay(n);
    if (ty->kind == TY_VOID) {
        c = mknode(N_CAST, &ty_void);
        c->a = n;
        return c;
    }
    if (isflt(ty) || isflt(n->ty))
        error_tok(tok(), "浮動小数点は扱わない", 0);
    if ((n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION) && !isaggr(ty))
        error_tok(tok(), "構造体・共用体はスカラーに変換できない", 0);
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && !compatible(unqual(ty), unqual(n->ty)))
        error_tok(tok(), "構造体・共用体へは変換できない", 0);
    if (n->op == N_NUM && (isint(ty) || isptr(ty))) {
        if (ty->kind == TY_BOOL)
            return mknum(n->val != 0, unqual(ty));
        return mknum(n->val, unqual(ty));
    }
    if (compatible(unqual(n->ty), unqual(ty)) && n->ty->kind != TY_BOOL
        && tsize(n->ty) == tsize(ty) && isunsignedt(n->ty) == isunsignedt(ty)) {
        if (n->ty->qual == 0)
            return n;
    }
    c = mknode(N_CAST, unqual(ty));
    c->a = n;
    return c;
}

/* 整数の昇格 */
static struct type *promoted(struct type *t)
{
    if (isint(t) && rank(t) < 3)
        return &ty_int;
    if (t->kind == TY_ENUM)
        return t->sd->enumbase ? t->sd->enumbase : &ty_uint;
    return t;
}

static struct node *promote(struct node *n)
{
    n = decay(n);
    if (isint(n->ty))
        return conv(n, promoted(n->ty));
    return n;
}

/* 通常の算術変換 (ILP32: long は 32 bit) */
static struct type *usual(struct type *a, struct type *b)
{
    a = promoted(a);
    b = promoted(b);
    if (a->kind == TY_ULLONG || b->kind == TY_ULLONG)
        return &ty_ullong;
    if (a->kind == TY_LLONG || b->kind == TY_LLONG)
        return &ty_llong;
    if (a->kind == TY_ULONG || b->kind == TY_ULONG)
        return &ty_ulong;
    if (a->kind == TY_LONG || b->kind == TY_LONG)
        return (a->kind == TY_UINT || b->kind == TY_UINT) ? &ty_ulong : &ty_long;
    if (a->kind == TY_UINT || b->kind == TY_UINT)
        return &ty_uint;
    return &ty_int;
}

/* 2 つの定数を演算する。結果は型 t に切り詰める */
static i64 foldop(int op, i64 x, i64 y, struct type *t, int *ok)
{
    int u = isunsignedt(t);
    u64 ux = (u64)x, uy = (u64)y;
    *ok = 1;
    switch (op) {
    case N_ADD: return wrap((i64)(ux + uy), t);
    case N_SUB: return wrap((i64)(ux - uy), t);
    case N_MUL: return wrap((i64)(ux * uy), t);
    case N_DIV:
    case N_MOD:
        if (y == 0) {
            *ok = 0;
            return 0;
        }
        if (u) {
            if (tsize(t) == 4) {
                ux &= 0xffffffffULL;
                uy &= 0xffffffffULL;
            }
            return wrap((i64)(op == N_DIV ? ux / uy : ux % uy), t);
        }
        if (y == -1)
            return wrap(op == N_DIV ? (i64)(0 - ux) : 0, t);
        return wrap(op == N_DIV ? x / y : x % y, t);
    case N_SHL: return wrap((i64)(ux << (y & 63)), t);
    case N_SHR:
        if (u)
            return wrap((i64)((tsize(t) == 4 ? (ux & 0xffffffffULL) : ux) >> (y & 63)), t);
        return wrap(x >> (y & 63), t);
    case N_AND: return wrap(x & y, t);
    case N_OR: return wrap(x | y, t);
    case N_XOR: return wrap(x ^ y, t);
    }
    *ok = 0;
    return 0;
}

static int cmpconst(int op, i64 x, i64 y, int u)
{
    if (u) {
        u64 a = (u64)x, b = (u64)y;
        switch (op) {
        case N_LT: return a < b;
        case N_LE: return a <= b;
        case N_GT: return a > b;
        case N_GE: return a >= b;
        }
    }
    switch (op) {
    case N_EQ: return x == y;
    case N_NE: return x != y;
    case N_LT: return x < y;
    case N_LE: return x <= y;
    case N_GT: return x > y;
    case N_GE: return x >= y;
    }
    return 0;
}

static struct node *mkbin(int op, struct type *t, struct node *a, struct node *b)
{
    struct node *n;
    if (a->op == N_NUM && b->op == N_NUM && isint(t)) {
        int ok;
        i64 v = foldop(op, a->val, b->val, t, &ok);
        if (ok)
            return mknum(v, t);
    }
    n = mknode(op, t);
    n->a = a;
    n->b = b;
    return n;
}

static int is_nullptr(struct node *n)
{
    i64 v;
    if (n->op == N_CAST && n->ty->kind == TY_PTR && n->ty->base->kind == TY_VOID)
        n = n->a;
    return isint(n->ty) && eval_int(n, &v) && v == 0;
}

/* ポインタ + 整数 (整数は要素の大きさを掛けて足す) */
static struct node *ptradd(int op, struct node *p, struct node *i)
{
    i64 sz = tsize(p->ty->base);
    struct node *n;
    if (p->ty->base->kind == TY_VOID || p->ty->base->kind == TY_FUNC)
        sz = 1;             /* GCC の拡張: void * と関数へのポインタの算術 */
    i = conv(i, is64(i->ty) ? &ty_int : (isunsignedt(i->ty) && rank(i->ty) >= 3 ? &ty_uint : &ty_int));
    i = mkbin(N_MUL, i->ty, i, mknum(sz, i->ty));
    if (p->op == N_NUM && i->op == N_NUM)
        return mknum(op == N_ADD ? p->val + i->val : p->val - i->val, p->ty);
    n = mknode(op, p->ty);
    n->a = p;
    n->b = i;
    return n;
}

static struct node *binop(int op, struct node *a, struct node *b)
{
    struct type *t;
    a = decay(a);
    b = decay(b);
    switch (op) {
    case N_ADD:
        if (isptr(a->ty) && isint(b->ty))
            return ptradd(N_ADD, a, b);
        if (isint(a->ty) && isptr(b->ty))
            return ptradd(N_ADD, b, a);
        break;
    case N_SUB:
        if (isptr(a->ty) && isint(b->ty))
            return ptradd(N_SUB, a, b);
        if (isptr(a->ty) && isptr(b->ty)) {
            struct node *n;
            i64 sz = tsize(a->ty->base);
            if (a->ty->base->kind == TY_VOID || sz == 0)
                sz = 1;
            if (a->op == N_NUM && b->op == N_NUM)
                return mknum((a->val - b->val) / sz, &ty_int);
            n = mknode(N_PSUB, &ty_int);
            n->a = a;
            n->b = b;
            n->val = sz;
            return n;
        }
        break;
    case N_SHL:
    case N_SHR:
        if (!isint(a->ty) || !isint(b->ty))
            perr("シフトの被演算子が整数でない");
        a = promote(a);
        b = conv(b, &ty_int);
        return mkbin(op, a->ty, a, b);
    case N_EQ: case N_NE: case N_LT: case N_LE: case N_GT: case N_GE: {
        struct node *n;
        int u;
        if (isptr(a->ty) || isptr(b->ty)) {
            if (!isptr(a->ty))
                a = conv(a, b->ty);
            if (!isptr(b->ty))
                b = conv(b, a->ty);
            u = 1;
        } else {
            if (!isarith(a->ty) || !isarith(b->ty))
                perr("比較の被演算子が算術型でない");
            t = usual(a->ty, b->ty);
            a = conv(a, t);
            b = conv(b, t);
            u = isunsignedt(t);
        }
        if (a->op == N_NUM && b->op == N_NUM)
            return mknum(cmpconst(op, a->val, b->val, u), &ty_int);
        n = mknode(op, &ty_int);
        n->a = a;
        n->b = b;
        return n;
    }
    case N_LAND:
    case N_LOR: {
        struct node *n;
        i64 x, y;
        if (!isscalar(a->ty) || !isscalar(b->ty))
            perr("論理演算の被演算子がスカラーでない");
        if (eval_int(a, &x)) {
            /* 左が定数なら右を評価しない形に畳む (Linux の IS_ENABLED など) */
            if (op == N_LAND && !x)
                return mknum(0, &ty_int);
            if (op == N_LOR && x)
                return mknum(1, &ty_int);
            if (eval_int(b, &y))
                return mknum(y != 0, &ty_int);
            return binop(N_NE, b, mknum(0, &ty_int));
        }
        n = mknode(op, &ty_int);
        n->a = a;
        n->b = b;
        return n;
    }
    }
    if (!isarith(a->ty) || !isarith(b->ty))
        perr("算術の被演算子が算術型でない");
    if ((op == N_MOD || op == N_AND || op == N_OR || op == N_XOR)
        && (!isint(a->ty) || !isint(b->ty)))
        perr("整数でない被演算子");
    t = usual(a->ty, b->ty);
    return mkbin(op, t, conv(a, t), conv(b, t));
}

/* 代入の右辺を左辺の型に合わせる */
static struct node *assign_conv(struct node *r, struct type *lt)
{
    r = decay(r);
    if (lt->kind == TY_STRUCT || lt->kind == TY_UNION) {
        if (!compatible(unqual(r->ty), unqual(lt)))
            perr("構造体の型が合わない代入");
        return r;
    }
    if (lt->kind == TY_BOOL && isscalar(r->ty)) {
        if (isptr(r->ty))
            return binop(N_NE, r, mknum(0, &ty_int));
        return conv(r, lt);
    }
    if (isptr(lt) && !isptr(r->ty) && !isint(r->ty))
        perr("ポインタへの代入の型が合わない");
    return conv(r, lt);
}

/* ==== 式の構文 ==== */

static struct node *cast_expr(void);
static struct node *unary_expr(void);
static struct node *postfix_expr(void);

static struct node *mkvar(struct sym *s)
{
    struct node *n = mknode(N_VAR, s->ty);
    n->sym = s;
    return n;
}

static struct node *mkderef(struct node *p)
{
    struct node *n;
    p = decay(p);
    if (!isptr(p->ty))
        perr("ポインタでないものを参照した");
    n = mknode(N_DEREF, p->ty->base);
    n->a = p;
    return n;
}

static struct node *mkaddr(struct node *e)
{
    struct node *n;
    if (e->ty->kind == TY_FUNC || e->ty->kind == TY_ARRAY) {
        n = mknode(N_ADDR, ptrto(e->ty));
        n->a = e;
        return n;
    }
    if (e->op == N_DEREF)
        return conv(e->a, ptrto(e->ty));
    if (!is_lvalue(e))
        perr("左辺値でないものの番地を取った");
    if (e->op == N_VAR)
        e->sym->addrtaken = 1;
    n = mknode(N_ADDR, ptrto(e->ty));
    n->a = e;
    return n;
}

static struct node *mkmember(struct node *s, struct name *mn)
{
    struct node *n;
    struct member *m;
    i64 off;
    if (s->ty->kind != TY_STRUCT && s->ty->kind != TY_UNION)
        perr("構造体でないものの成員を引いた");
    if (!s->ty->sd->complete)
        perr("不完全な構造体の成員を引いた");
    m = find_member(s->ty, mn, &off);
    if (!m)
        error_tok(tok(), "成員が無い", mn->s);
    n = mknode(N_MEMBER, qualify(m->ty, s->ty->qual));
    n->a = s;
    n->mem = m;
    n->val = off;
    return n;
}

static struct node *mkstr(const char *s, long len, int wide)
{
    struct strlit *l = xmalloc(sizeof(struct strlit));
    struct node *n;
    l->s = xstrndup(s, len);
    l->len = len;
    l->id = nstrlit++;
    l->wide = wide;
    *strtail = l;
    strtail = &l->next;
    n = mknode(N_STR, arrayof(wide ? &ty_int : &ty_char, len + 1));
    n->str = l;
    return n;
}

/* 数の字句の型 (C90 と GNU の規則) */
static struct node *number(struct token *t)
{
    static struct type *dec0[] = { &ty_int, &ty_long, &ty_ulong, &ty_llong, &ty_ullong, 0 };
    static struct type *hex0[] = { &ty_int, &ty_uint, &ty_long, &ty_ulong, &ty_llong, &ty_ullong, 0 };
    static struct type *u0[] = { &ty_uint, &ty_ulong, &ty_ullong, 0 };
    static struct type *dl[] = { &ty_long, &ty_ulong, &ty_llong, &ty_ullong, 0 };
    static struct type *ul[] = { &ty_ulong, &ty_ullong, 0 };
    static struct type *ll[] = { &ty_llong, &ty_ullong, 0 };
    static struct type *ull[] = { &ty_ullong, 0 };
    struct type **c;
    int i;
    if (t->nlong >= 2)
        c = t->isunsigned ? ull : ll;
    else if (t->nlong == 1)
        c = t->isunsigned ? ul : dl;
    else if (t->isunsigned)
        c = u0;
    else
        c = t->hexoct ? hex0 : dec0;
    for (i = 0; c[i]; i++) {
        struct type *ty = c[i];
        u64 max = ty->size == 8 ? (isunsignedt(ty) ? ~0ULL : 0x7fffffffffffffffULL)
            : (isunsignedt(ty) ? 0xffffffffULL : 0x7fffffffULL);
        if (t->val <= max)
            return mknum((i64)t->val, ty);
    }
    return mknum((i64)t->val, &ty_ullong);
}

static struct node *funcall(struct node *f)
{
    struct type *ft;
    struct node *n, head, *tail = &head;
    struct param *p;
    head.next = 0;
    f = decay(f);
    if (!isptr(f->ty) || f->ty->base->kind != TY_FUNC)
        perr("関数でないものを呼んだ");
    ft = f->ty->base;
    p = ft->params;
    if (!accept_p(')')) {
        for (;;) {
            struct node *arg = assign_expr();
            if (p) {
                arg = assign_conv(arg, p->ty);
                p = p->next;
            } else {
                if (ft->proto && !ft->variadic)
                    perr("引数が多すぎる");
                /* 既定の実引数昇格 */
                arg = decay(arg);
                if (isint(arg->ty))
                    arg = promote(arg);
            }
            tail->next = arg;
            tail = arg;
            if (!accept_p(','))
                break;
        }
        expect_p(')');
    }
    if (p && ft->proto)
        perr("引数が少なすぎる");
    n = mknode(N_CALL, ft->base);
    n->a = f;
    n->b = head.next;
    return n;
}

static struct node *stmt_expr(void);
static struct node *builtin_call(struct name *n);

static struct node *primary_expr(void)
{
    struct token *t = tok();
    struct node *n;
    if (accept_p('(')) {
        if (ispunct_('{')) {
            n = stmt_expr();
            expect_p(')');
            return n;
        }
        n = expr();
        expect_p(')');
        return n;
    }
    if (t->kind == T_NUM) {
        advance();
        return number(t);
    }
    if (t->kind == T_FNUM)
        perr("浮動小数点の定数は扱わない");
    if (t->kind == T_CHAR) {
        advance();
        /* 1 文字の定数は (char) の値を int にしたもの。char は unsigned */
        return mknum(t->wide ? (i64)t->val : (i64)(int)(unsigned)t->val, &ty_int);
    }
    if (t->kind == T_STR) {
        struct buf b;
        int wide = 0;
        memset(&b, 0, sizeof b);
        while (tok()->kind == T_STR) {
            wide |= tok()->wide;
            bput(&b, tok()->str, tok()->slen);
            advance();
        }
        if (!b.p)
            bput(&b, "", 0);
        return mkstr(b.p, b.n, wide);
    }
    if (t->kind == T_IDENT) {
        struct sym *s = t->id->ord;
        if (strncmp(t->id->s, "__builtin_", 10) == 0 && (!s || s->kind != SY_FUNC || !s->global)) {
            struct node *bn = builtin_call(t->id);
            if (bn)
                return bn;
        }
        if (!s) {
            if (strcmp(t->id->s, "__func__") == 0 || strcmp(t->id->s, "__FUNCTION__") == 0
                || strcmp(t->id->s, "__PRETTY_FUNCTION__") == 0) {
                advance();
                if (!curfn)
                    perr("__func__ が関数の外にある");
                return mkstr(curfn->name->s, curfn->name->len, 0);
            }
            error_tok(t, "宣言されていない名前", t->id->s);
        }
        advance();
        if (s->alias_of)
            s = s->alias_of;
        if (s->kind == SY_ENUMC)
            return mknum(s->val, s->ty);
        if (s->kind == SY_TYPEDEF)
            error_tok(t, "型の名前を式に使った", t->id->s);
        return mkvar(s);
    }
    perr("式が要る");
    return 0;
}

static struct node *postfix_expr(void)
{
    struct node *n = primary_expr();
    for (;;) {
        if (accept_p('[')) {
            struct node *i = expr();
            expect_p(']');
            n = mkderef(binop(N_ADD, n, i));
            continue;
        }
        if (accept_p('(')) {
            n = funcall(n);
            continue;
        }
        if (accept_p('.')) {
            n = mkmember(n, expect_ident());
            continue;
        }
        if (accept_p(P_ARROW)) {
            n = mkmember(mkderef(n), expect_ident());
            continue;
        }
        if (ispunct_(P_INC) || ispunct_(P_DEC)) {
            int dec = ispunct_(P_DEC);
            struct node *m;
            advance();
            if (!is_lvalue(n))
                perr("++ / -- の被演算子が左辺値でない");
            m = mknode(N_POSTINC, n->ty);
            m->a = n;
            m->val = isptr(n->ty) ? tsize(n->ty->base) : 1;
            if (isptr(n->ty) && n->ty->base->kind == TY_VOID)
                m->val = 1;
            if (dec)
                m->val = -m->val;
            n = m;
            continue;
        }
        return n;
    }
}

static struct node *sizeof_type(struct type *t)
{
    if (t->kind == TY_FUNC || t->kind == TY_VOID)
        return mknum(1, &ty_uint);
    return mknum(tsize(t), &ty_uint);
}

static struct node *unary_expr(void)
{
    struct token *t = tok();
    if (t->kind == T_PUNCT) {
        switch (t->punct) {
        case P_INC:
        case P_DEC: {
            struct node *e, *m;
            advance();
            e = unary_expr();
            if (!is_lvalue(e))
                perr("++ / -- の被演算子が左辺値でない");
            m = mknode(N_PREINC, e->ty);
            m->a = e;
            m->val = isptr(e->ty) ? tsize(e->ty->base) : 1;
            if (isptr(e->ty) && e->ty->base->kind == TY_VOID)
                m->val = 1;
            if (t->punct == P_DEC)
                m->val = -m->val;
            return m;
        }
        case '&':
            advance();
            if (accept_p(P_ANDAND))
                perr("ラベルの番地 (&&label) は扱わない");
            return mkaddr(cast_expr());
        case '*':
            advance();
            return mkderef(cast_expr());
        case '+':
            advance();
            return promote(cast_expr());
        case '-': {
            struct node *e;
            advance();
            e = promote(cast_expr());
            if (!isarith(e->ty))
                perr("単項 - の被演算子が算術型でない");
            return mkbin(N_SUB, e->ty, mknum(0, e->ty), e);
        }
        case '~': {
            struct node *e, *n;
            advance();
            e = promote(cast_expr());
            if (!isint(e->ty))
                perr("~ の被演算子が整数でない");
            if (e->op == N_NUM)
                return mknum(~e->val, e->ty);
            n = mknode(N_BNOT, e->ty);
            n->a = e;
            return n;
        }
        case '!': {
            struct node *e, *n;
            advance();
            e = decay(cast_expr());
            if (!isscalar(e->ty))
                perr("! の被演算子がスカラーでない");
            if (e->op == N_NUM)
                return mknum(!e->val, &ty_int);
            n = mknode(N_NOT, &ty_int);
            n->a = e;
            return n;
        }
        }
    }
    if (t->kind == T_KW && t->id->kw == K_SIZEOF) {
        advance();
        if (ispunct_('(') && is_typename(peekn(1))) {
            struct type *ty;
            advance();
            ty = typename_();
            expect_p(')');
            if (ispunct_('{'))
                perr("複合リテラルは扱わない");
            return sizeof_type(ty);
        }
        return sizeof_type(unary_expr()->ty);
    }
    if (t->kind == T_KW && t->id->kw == K_ALIGNOF) {
        struct type *ty;
        advance();
        if (ispunct_('(') && is_typename(peekn(1))) {
            advance();
            ty = typename_();
            expect_p(')');
        } else
            ty = unary_expr()->ty;
        return mknum(talign(ty), &ty_uint);
    }
    if (t->kind == T_KW && t->id->kw == K_EXTENSION) {
        advance();
        return cast_expr();
    }
    return postfix_expr();
}

static struct node *cast_expr(void)
{
    if (ispunct_('(') && is_typename(peekn(1))) {
        struct type *t;
        struct node *e, *c;
        advance();
        t = typename_();
        expect_p(')');
        if (ispunct_('{'))
            perr("複合リテラルは扱わない");
        e = cast_expr();
        if (t->kind == TY_STRUCT || t->kind == TY_UNION)
            perr("構造体へのキャストは扱わない");
        e = decay(e);
        if (t->kind != TY_VOID && !isscalar(e->ty))
            perr("スカラーでないもののキャスト");
        c = conv(e, t);
        if (c == e && t->kind != TY_VOID) {
            /* 型を替えないキャストでも値 (右辺値) にする */
            c = mknode(N_CAST, unqual(t));
            c->a = e;
        }
        return c;
    }
    return unary_expr();
}

static int binprec(int *op)
{
    struct token *t = tok();
    if (t->kind != T_PUNCT)
        return 0;
    switch (t->punct) {
    case P_OROR: *op = N_LOR; return 1;
    case P_ANDAND: *op = N_LAND; return 2;
    case '|': *op = N_OR; return 3;
    case '^': *op = N_XOR; return 4;
    case '&': *op = N_AND; return 5;
    case P_EQ: *op = N_EQ; return 6;
    case P_NE: *op = N_NE; return 6;
    case '<': *op = N_LT; return 7;
    case '>': *op = N_GT; return 7;
    case P_LE: *op = N_LE; return 7;
    case P_GE: *op = N_GE; return 7;
    case P_SHL: *op = N_SHL; return 8;
    case P_SHR: *op = N_SHR; return 8;
    case '+': *op = N_ADD; return 9;
    case '-': *op = N_SUB; return 9;
    case '*': *op = N_MUL; return 10;
    case '/': *op = N_DIV; return 10;
    case '%': *op = N_MOD; return 10;
    }
    return 0;
}

static struct node *bin_expr(int minprec)
{
    struct node *l = cast_expr();
    for (;;) {
        int op, p = binprec(&op);
        struct node *r;
        if (!p || p < minprec)
            return l;
        advance();
        r = bin_expr(p + 1);
        l = binop(op, l, r);
    }
}

static struct node *cond_expr(void)
{
    struct node *c = bin_expr(1), *a, *b, *n;
    struct type *t;
    i64 cv;
    if (!accept_p('?'))
        return c;
    c = decay(c);
    if (!isscalar(c->ty))
        perr("?: の条件がスカラーでない");
    if (ispunct_(':')) {
        /* GNU の x ?: y */
        perr("?: の中を省いた形は扱わない");
    }
    a = decay(expr());
    expect_p(':');
    b = decay(cond_expr());
    if (isarith(a->ty) && isarith(b->ty)) {
        t = usual(a->ty, b->ty);
        a = conv(a, t);
        b = conv(b, t);
    } else if (a->ty->kind == TY_VOID || b->ty->kind == TY_VOID) {
        t = &ty_void;
    } else if (isptr(a->ty) && isptr(b->ty)) {
        /* void * と他のポインタは void * にする */
        if (a->ty->base->kind == TY_VOID && !is_nullptr(a))
            t = a->ty;
        else if (b->ty->base->kind == TY_VOID && !is_nullptr(b))
            t = b->ty;
        else if (is_nullptr(a))
            t = b->ty;
        else
            t = a->ty;
        a = conv(a, t);
        b = conv(b, t);
    } else if (isptr(a->ty) && is_nullptr(b)) {
        t = a->ty;
        b = conv(b, t);
    } else if (isptr(b->ty) && is_nullptr(a)) {
        t = b->ty;
        a = conv(a, t);
    } else if ((a->ty->kind == TY_STRUCT || a->ty->kind == TY_UNION)
               && compatible(unqual(a->ty), unqual(b->ty))) {
        t = unqual(a->ty);
    } else {
        perr("?: の 2 つの値の型が合わない");
        t = 0;
    }
    if (eval_int(c, &cv) && t->kind != TY_STRUCT && t->kind != TY_UNION)
        return cv ? a : b;
    n = mknode(N_COND, t);
    n->a = c;
    n->b = a;
    n->c = b;
    return n;
}

static int assignop(int *op)
{
    struct token *t = tok();
    if (t->kind != T_PUNCT)
        return 0;
    switch (t->punct) {
    case '=': *op = 0; return 1;
    case P_MULEQ: *op = N_MUL; return 1;
    case P_DIVEQ: *op = N_DIV; return 1;
    case P_MODEQ: *op = N_MOD; return 1;
    case P_ADDEQ: *op = N_ADD; return 1;
    case P_SUBEQ: *op = N_SUB; return 1;
    case P_SHLEQ: *op = N_SHL; return 1;
    case P_SHREQ: *op = N_SHR; return 1;
    case P_ANDEQ: *op = N_AND; return 1;
    case P_XOREQ: *op = N_XOR; return 1;
    case P_OREQ: *op = N_OR; return 1;
    }
    return 0;
}

static struct node *assign_expr(void)
{
    struct node *l = cond_expr(), *r, *n;
    int op;
    if (!assignop(&op))
        return l;
    advance();
    if (!is_lvalue(l))
        perr("代入の左辺が左辺値でない");
    if (l->ty->kind == TY_ARRAY || l->ty->kind == TY_FUNC)
        perr("配列や関数には代入できない");
    r = assign_expr();
    if (op == 0) {
        n = mknode(N_ASSIGN, unqual(l->ty));
        n->a = l;
        n->b = assign_conv(r, l->ty);
        return n;
    }
    n = mknode(N_OPASSIGN, unqual(l->ty));
    n->a = l;
    n->val = op;
    r = decay(r);
    if (isptr(l->ty) && (op == N_ADD || op == N_SUB)) {
        i64 sz = tsize(l->ty->base);
        if (l->ty->base->kind == TY_VOID)
            sz = 1;
        r = conv(r, &ty_int);
        n->b = mkbin(N_MUL, &ty_int, r, mknum(sz, &ty_int));
        n->ctype = l->ty;
        return n;
    }
    if (!isarith(l->ty) || !isarith(r->ty))
        perr("複合代入の被演算子が算術型でない");
    if (op == N_SHL || op == N_SHR) {
        n->ctype = promoted(l->ty);
        n->b = conv(r, &ty_int);
    } else {
        n->ctype = usual(l->ty, r->ty);
        n->b = conv(r, n->ctype);
    }
    return n;
}

static struct node *expr(void)
{
    struct node *n = assign_expr();
    while (accept_p(',')) {
        struct node *r = assign_expr(), *c;
        c = mknode(N_COMMA, r->ty);
        c->a = n;
        c->b = r;
        n = c;
    }
    return n;
}

/* ==== 組込み関数 (docs/stage018-ncc.md 4.8) ==== */

static struct node *mkvoid(struct node *e)
{
    struct node *c = mknode(N_CAST, &ty_void);
    c->a = e;
    return c;
}

/* __builtin_offsetof(型, 成員の指示) */
static struct node *builtin_offsetof(void)
{
    struct type *t;
    i64 off = 0;
    t = typename_();
    expect_p(',');
    for (;;) {
        if (tok()->kind == T_IDENT || accept_p('.')) {
            struct name *n = expect_ident();
            struct member *m;
            i64 o;
            if (t->kind != TY_STRUCT && t->kind != TY_UNION)
                perr("offsetof の成員が構造体の中に無い");
            m = find_member(t, n, &o);
            if (!m)
                error_tok(tok(), "成員が無い", n->s);
            off += o;
            t = m->ty;
        } else if (accept_p('[')) {
            i64 i = const_expr();
            expect_p(']');
            if (t->kind != TY_ARRAY)
                perr("offsetof の添字が配列でない");
            t = t->base;
            off += i * tsize(t);
        } else
            break;
        if (ispunct_(')'))
            break;
    }
    expect_p(')');
    return mknum(off, &ty_uint);
}

/* 組込み関数の呼出し。知らない名前なら 0 を返す (普通の名前として引く) */
static struct node *builtin_call(struct name *nm)
{
    const char *s = nm->s + 10;
    struct node *n, *e;
    if (strcmp(s, "va_start") == 0) {
        advance();
        expect_p('(');
        e = unary_expr();
        expect_p(',');
        assign_expr();
        expect_p(')');
        if (!curfn || !curfn->ty->variadic)
            perr("可変長でない関数の中の va_start");
        n = mknode(N_VASTART, &ty_void);
        n->a = e;
        return n;
    }
    if (strcmp(s, "va_arg") == 0) {
        struct type *t;
        advance();
        expect_p('(');
        e = assign_expr();
        expect_p(',');
        t = typename_();
        expect_p(')');
        n = mknode(N_VAARG, t);
        n->a = e;
        return n;
    }
    if (strcmp(s, "va_end") == 0) {
        advance();
        expect_p('(');
        e = assign_expr();
        expect_p(')');
        return mkvoid(e);
    }
    if (strcmp(s, "va_copy") == 0) {
        struct node *d;
        advance();
        expect_p('(');
        d = unary_expr();
        expect_p(',');
        e = assign_expr();
        expect_p(')');
        n = mknode(N_ASSIGN, unqual(d->ty));
        n->a = d;
        n->b = conv(e, d->ty);
        return mkvoid(n);
    }
    if (strcmp(s, "expect") == 0) {
        advance();
        expect_p('(');
        e = assign_expr();
        expect_p(',');
        assign_expr();
        expect_p(')');
        return conv(e, &ty_long);
    }
    if (strcmp(s, "constant_p") == 0) {
        i64 v;
        advance();
        expect_p('(');
        e = assign_expr();
        expect_p(')');
        return mknum(eval_int(decay(e), &v) ? 1 : 0, &ty_int);
    }
    if (strcmp(s, "offsetof") == 0) {
        advance();
        expect_p('(');
        return builtin_offsetof();
    }
    if (strcmp(s, "types_compatible_p") == 0) {
        struct type *a, *b;
        advance();
        expect_p('(');
        a = typename_();
        expect_p(',');
        b = typename_();
        expect_p(')');
        return mknum(compatible(unqual(a), unqual(b)), &ty_int);
    }
    if (strcmp(s, "choose_expr") == 0) {
        i64 c;
        struct node *a, *b;
        advance();
        expect_p('(');
        c = const_expr();
        expect_p(',');
        a = assign_expr();
        expect_p(',');
        b = assign_expr();
        expect_p(')');
        return c ? a : b;
    }
    if (strcmp(s, "unreachable") == 0 || strcmp(s, "trap") == 0) {
        advance();
        expect_p('(');
        expect_p(')');
        n = mknode(N_BUILTIN, &ty_void);
        n->val = s[0] == 't';
        return n;
    }
    return 0;
}

/* ({ ... }) の値は最後の式文の値 */
static struct node *stmt_expr(void)
{
    struct node *b = compound_stmt(1), *last = 0, *s, *n;
    for (s = b->a; s; s = s->next)
        last = s;
    n = mknode(N_STMTEXPR, &ty_void);
    n->a = b;
    if (last && last->op == S_EXPR) {
        last->a = decay(last->a);
        n->ty = last->a->ty;
    }
    return n;
}

/* ==== 初期化子 ==== */

struct ilist {
    struct ient *head;
    struct ient **tail;
};

static void iadd(struct ilist *l, i64 off, struct type *ty, struct node *e, struct member *bf)
{
    struct ient *x = xmalloc(sizeof(struct ient));
    x->off = off;
    x->ty = ty;
    x->expr = e;
    x->bf = bf;
    *l->tail = x;
    l->tail = &x->next;
}

static int ischararray(struct type *t)
{
    return t->kind == TY_ARRAY && (t->base->kind == TY_CHAR || t->base->kind == TY_SCHAR
                                   || t->base->kind == TY_UCHAR
                                   || (t->base->kind == TY_INT && 0));
}

static i64 init_value(struct ilist *l, struct type *ty, i64 off, struct member *bf,
                      struct node *pending);
static i64 init_braced(struct ilist *l, struct type *ty, i64 off);

/* 構造体の次の成員 (名前の無いビットフィールドは飛ばす) */
static struct member *next_member(struct member *m)
{
    while (m && !m->name && m->bw >= 0)
        m = m->next;
    return m;
}

/* 中括弧の無い集成体: 葉を順に埋める */
static void init_elided(struct ilist *l, struct type *ty, i64 off, struct node *pending)
{
    int first = 1;
    if (ty->kind == TY_ARRAY) {
        i64 i;
        if (ty->len < 0)
            perr("大きさの決まらない配列を中括弧なしで初期化した");
        for (i = 0; i < ty->len; i++) {
            if (!first) {
                if (!ispunct_(',') || (peekn(1)->kind == T_PUNCT
                    && (peekn(1)->punct == '}' || peekn(1)->punct == '.' || peekn(1)->punct == '[')))
                    return;
                advance();
            }
            init_value(l, ty->base, off + i * tsize(ty->base), 0, first ? pending : 0);
            first = 0;
        }
        return;
    }
    {
        struct member *m = next_member(ty->sd->mem);
        for (; m; m = next_member(m->next)) {
            if (!first) {
                if (!ispunct_(',') || (peekn(1)->kind == T_PUNCT
                    && (peekn(1)->punct == '}' || peekn(1)->punct == '.' || peekn(1)->punct == '[')))
                    return;
                advance();
            }
            init_value(l, m->ty, off + m->off, m->bw >= 0 ? m : 0, first ? pending : 0);
            first = 0;
            if (ty->kind == TY_UNION)
                return;
        }
    }
}

static i64 init_string(struct ilist *l, struct type *ty, i64 off)
{
    struct node *s;
    int braced = accept_p('{');
    s = primary_expr();
    if (braced) {
        accept_p(',');
        expect_p('}');
    }
    iadd(l, off, ty, s, 0);
    return s->str->len + 1;
}

/* 1 つの対象を初期化する。戻り値は配列の要素の数 (大きさを決めるため) */
static i64 init_value(struct ilist *l, struct type *ty, i64 off, struct member *bf,
                      struct node *pending)
{
    if (!pending && ischararray(ty)
        && (tok()->kind == T_STR || (ispunct_('{') && peekn(1)->kind == T_STR)))
        return init_string(l, ty, off);
    if (!pending && ispunct_('{')) {
        if (isaggr(ty)) {
            advance();
            return init_braced(l, ty, off);
        }
        /* スカラーを中括弧で囲んだもの */
        advance();
        {
            struct node *e = assign_expr();
            accept_p(',');
            expect_p('}');
            iadd(l, off, ty, assign_conv(e, ty), bf);
        }
        return 1;
    }
    if (isaggr(ty)) {
        struct node *e = pending ? pending : assign_expr();
        if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION)
            && compatible(unqual(e->ty), unqual(ty))) {
            iadd(l, off, ty, e, 0);
            return 1;
        }
        init_elided(l, ty, off, e);
        return 1;
    }
    {
        struct node *e = pending ? pending : assign_expr();
        iadd(l, off, ty, assign_conv(e, ty), bf);
    }
    return 1;
}

/* 指示子の鎖 (.a.b[2]) を辿り，= の後ろの値で初期化する */
static void init_designated(struct ilist *l, struct type *ty, i64 off)
{
    if (accept_p('[')) {
        i64 lo = const_expr(), hi = lo, i;
        struct ilist tmp;
        struct ient *x;
        if (accept_p(P_ELLIPSIS))
            hi = const_expr();
        expect_p(']');
        if (ty->kind != TY_ARRAY)
            perr("配列でないものに [] の指示子");
        tmp.head = 0;
        tmp.tail = &tmp.head;
        init_designated(&tmp, ty->base, 0);
        for (i = lo; i <= hi; i++)
            for (x = tmp.head; x; x = x->next)
                iadd(l, off + i * tsize(ty->base) + x->off, x->ty, x->expr, x->bf);
        return;
    }
    if (accept_p('.')) {
        struct name *n = expect_ident();
        struct member *m;
        i64 o;
        if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
            perr("構造体でないものに . の指示子");
        m = find_member(ty, n, &o);
        if (!m)
            error_tok(tok(), "成員が無い", n->s);
        if (ispunct_('.') || ispunct_('['))
            init_designated(l, m->ty, off + o);
        else {
            accept_p('=');
            init_value(l, m->ty, off + o, m->bw >= 0 ? m : 0, 0);
        }
        return;
    }
    accept_p('=');
    init_value(l, ty, off, 0, 0);
}

/* { の後ろ。} まで読む */
static i64 init_braced(struct ilist *l, struct type *ty, i64 off)
{
    if (ty->kind == TY_ARRAY) {
        i64 idx = 0, count = 0;
        i64 esz = tsize(ty->base);
        while (!accept_p('}')) {
            if (ispunct_('[')) {
                i64 lo, hi;
                long save = tp;
                advance();
                lo = const_expr();
                hi = lo;
                if (accept_p(P_ELLIPSIS))
                    hi = const_expr();
                tp = save;
                curfile = toks[tp].file;
                curline = toks[tp].line;
                init_designated(l, ty, off);
                idx = hi + 1;
            } else {
                if (ty->len >= 0 && idx >= ty->len)
                    perr("配列の初期化子が多すぎる");
                init_value(l, ty->base, off + idx * esz, 0, 0);
                idx++;
            }
            if (idx > count)
                count = idx;
            if (!accept_p(',')) {
                expect_p('}');
                break;
            }
        }
        return count;
    }
    {
        struct member *m = next_member(ty->sd->mem);
        while (!accept_p('}')) {
            if (ispunct_('.') || (tok()->kind == T_IDENT && peekn(1)->kind == T_PUNCT
                                  && peekn(1)->punct == ':')) {
                struct name *n;
                i64 o;
                struct member *dm, *top;
                if (tok()->kind == T_IDENT) {
                    /* GNU の古い形 name: value */
                    n = tok()->id;
                    advance();
                    advance();
                    dm = find_member(ty, n, &o);
                    if (!dm)
                        error_tok(tok(), "成員が無い", n->s);
                    init_value(l, dm->ty, off + o, dm->bw >= 0 ? dm : 0, 0);
                } else {
                    long save = tp;
                    advance();
                    n = expect_ident();
                    dm = find_member(ty, n, &o);
                    if (!dm)
                        error_tok(tok(), "成員が無い", n->s);
                    tp = save;
                    curfile = toks[tp].file;
                    curline = toks[tp].line;
                    init_designated(l, ty, off);
                }
                /* 続きはその成員を含む直下の成員の次から */
                for (top = ty->sd->mem; top; top = top->next) {
                    i64 o2;
                    if (top == dm || (!top->name && (top->ty->kind == TY_STRUCT
                                                     || top->ty->kind == TY_UNION)
                                      && find_member(top->ty, n, &o2)))
                        break;
                }
                m = top ? next_member(top->next) : 0;
            } else {
                if (!m) {
                    if (ty->kind == TY_UNION)
                        perr("共用体の初期化子が多すぎる");
                    perr("構造体の初期化子が多すぎる");
                }
                init_value(l, m->ty, off + m->off, m->bw >= 0 ? m : 0, 0);
                m = ty->kind == TY_UNION ? 0 : next_member(m->next);
            }
            if (!accept_p(',')) {
                expect_p('}');
                break;
            }
        }
        return 1;
    }
}

/* 初期化子を読む。大きさの決まらない配列なら *ty を長さの決まった型にする */
static struct ient *parse_initializer(struct type **ty, int isstatic)
{
    struct ilist l;
    i64 n;
    l.head = 0;
    l.tail = &l.head;
    (void)isstatic;
    n = init_value(&l, *ty, 0, 0, 0);
    if ((*ty)->kind == TY_ARRAY && (*ty)->len < 0) {
        struct type *t = arrayof((*ty)->base, n);
        t->qual = (*ty)->qual;
        *ty = t;
    }
    return l.head;
}

/* ==== 文 ==== */

static struct node *stmt(void);

struct swctx {
    struct node *sw;
    struct node **casetail;
    struct type *ty;
    struct swctx *up;
};

static struct swctx *cursw;
static int loopdepth;
static int nstaticlocal;

static struct node *mkstmt(int op)
{
    return mknode(op, 0);
}

static struct node *exprstmt(struct node *e)
{
    struct node *s = mkstmt(S_EXPR);
    s->a = e;
    return s;
}

static void static_assert_decl(void)
{
    i64 v;
    advance();
    expect_p('(');
    v = const_expr();
    if (accept_p(','))
        string_arg();
    expect_p(')');
    expect_p(';');
    if (!v)
        perr("_Static_assert が成り立たない");
}

static struct node *compound_stmt(int newscope)
{
    struct node *b = mkstmt(S_BLOCK), **tail = &b->a;
    expect_p('{');
    if (newscope)
        push_scope();
    while (accept_kw(K_LABEL)) {
        /* __label__ は局所のラベルの宣言。ラベルは関数ごとに持つので読み捨てる */
        for (;;) {
            expect_ident();
            if (!accept_p(','))
                break;
        }
        expect_p(';');
    }
    while (!accept_p('}')) {
        if (tok()->kind == T_EOF)
            perr("ブロックが閉じていない");
        if (is_declstart(tok()) && !(tok()->kind == T_IDENT && peekn(1)->kind == T_PUNCT
                                     && peekn(1)->punct == ':')) {
            declaration(0, &tail);
            continue;
        }
        if (iskw(K_EXTENSION) && is_declstart(peekn(1))) {
            advance();
            declaration(0, &tail);
            continue;
        }
        *tail = stmt();
        while (*tail)
            tail = &(*tail)->next;
    }
    if (newscope)
        pop_scope();
    return b;
}

static struct node *cond_of(void)
{
    struct node *c;
    expect_p('(');
    c = decay(expr());
    expect_p(')');
    if (!isscalar(c->ty))
        perr("条件がスカラーでない");
    return c;
}

static struct node *stmt(void)
{
    struct token *t = tok();
    struct node *s;
    if (ispunct_('{'))
        return compound_stmt(1);
    if (accept_p(';'))
        return mkstmt(S_NULL);
    if (t->kind == T_IDENT && peekn(1)->kind == T_PUNCT && peekn(1)->punct == ':') {
        struct attrs at;
        s = mkstmt(S_LABEL);
        s->lname = t->id;
        advance();
        advance();
        memset(&at, 0, sizeof at);
        parse_attrs(&at);
        if (ispunct_('}'))
            s->a = mkstmt(S_NULL);
        else
            s->a = stmt();
        return s;
    }
    if (t->kind == T_KW) {
        switch (t->id->kw) {
        case K_IF:
            advance();
            s = mkstmt(S_IF);
            s->a = cond_of();
            s->b = stmt();
            if (accept_kw(K_ELSE))
                s->c = stmt();
            return s;
        case K_WHILE:
            advance();
            s = mkstmt(S_WHILE);
            s->a = cond_of();
            loopdepth++;
            s->b = stmt();
            loopdepth--;
            return s;
        case K_DO:
            advance();
            s = mkstmt(S_DO);
            loopdepth++;
            s->b = stmt();
            loopdepth--;
            if (!accept_kw(K_WHILE))
                perr("do の後ろに while が要る");
            s->a = cond_of();
            expect_p(';');
            return s;
        case K_FOR: {
            struct node *init = 0;
            advance();
            expect_p('(');
            push_scope();
            s = mkstmt(S_FOR);
            if (is_declstart(tok())) {
                struct node *blk = mkstmt(S_BLOCK), **tail = &blk->a;
                declaration(0, &tail);
                init = blk;
            } else {
                if (!ispunct_(';'))
                    init = exprstmt(expr());
                expect_p(';');
            }
            s->a = init;
            if (!ispunct_(';')) {
                s->b = decay(expr());
                if (!isscalar(s->b->ty))
                    perr("条件がスカラーでない");
            }
            expect_p(';');
            if (!ispunct_(')'))
                s->c = expr();
            expect_p(')');
            loopdepth++;
            s->d = stmt();
            loopdepth--;
            pop_scope();
            return s;
        }
        case K_SWITCH: {
            struct swctx ctx;
            struct node *e;
            advance();
            expect_p('(');
            e = promote(expr());
            expect_p(')');
            if (!isint(e->ty))
                perr("switch の値が整数でない");
            s = mkstmt(S_SWITCH);
            s->a = e;
            ctx.sw = s;
            ctx.casetail = &s->cases;
            ctx.ty = e->ty;
            ctx.up = cursw;
            cursw = &ctx;
            loopdepth++;
            s->b = stmt();
            loopdepth--;
            cursw = ctx.up;
            return s;
        }
        case K_CASE: {
            i64 lo, hi;
            advance();
            if (!cursw)
                perr("switch の外の case");
            lo = const_expr();
            hi = lo;
            if (accept_p(P_ELLIPSIS))
                hi = const_expr();
            expect_p(':');
            s = mkstmt(S_CASE);
            s->val = wrap(lo, cursw->ty);
            s->val2 = wrap(hi, cursw->ty);
            *cursw->casetail = s;
            cursw->casetail = &s->nextcase;
            s->a = ispunct_('}') ? mkstmt(S_NULL) : stmt();
            return s;
        }
        case K_DEFAULT:
            advance();
            expect_p(':');
            if (!cursw)
                perr("switch の外の default");
            s = mkstmt(S_DEFAULT);
            *cursw->casetail = s;
            cursw->casetail = &s->nextcase;
            s->a = ispunct_('}') ? mkstmt(S_NULL) : stmt();
            return s;
        case K_BREAK:
            advance();
            expect_p(';');
            if (!loopdepth)
                perr("ループと switch の外の break");
            return mkstmt(S_BREAK);
        case K_CONTINUE:
            advance();
            expect_p(';');
            return mkstmt(S_CONTINUE);
        case K_RETURN: {
            struct type *rt = curfn->ty->base;
            advance();
            s = mkstmt(S_RETURN);
            if (!ispunct_(';')) {
                struct node *e = expr();
                if (rt->kind == TY_VOID)
                    s->a = mkvoid(e);
                else
                    s->a = assign_conv(e, rt);
            }
            expect_p(';');
            return s;
        }
        case K_GOTO:
            advance();
            if (ispunct_('*'))
                perr("計算した goto は扱わない");
            s = mkstmt(S_GOTO);
            s->lname = expect_ident();
            expect_p(';');
            return s;
        case K_ASM:
            perr("asm 文は M4 で扱う");
            break;
        }
    }
    s = exprstmt(expr());
    expect_p(';');
    return s;
}

/* ==== 宣言 ==== */

static char *labelof(struct sym *s)
{
    if (s->at.asmlabel)
        return s->at.asmlabel;
    return s->name->s;
}

static void merge_attrs(struct attrs *d, struct attrs *s)
{
    if (s->aligned > d->aligned) d->aligned = s->aligned;
    if (s->packed) d->packed = 1;
    if (s->section) d->section = s->section;
    if (s->weak) d->weak = 1;
    if (s->alias) d->alias = s->alias;
    if (s->used) d->used = 1;
    if (s->unused) d->unused = 1;
    if (s->noreturn) d->noreturn = 1;
    if (s->gnu_inline) d->gnu_inline = 1;
    if (s->always_inline) d->always_inline = 1;
    if (s->error) d->error = s->error;
    if (s->mode) d->mode = s->mode;
    if (s->asmlabel) d->asmlabel = s->asmlabel;
    if (s->regname) d->regname = s->regname;
    if (s->hidden) d->hidden = 1;
}

/* ファイルの有効範囲の記号を引くか作る (再宣言はまとめる) */
static struct sym *global_sym(struct name *n, int kind, struct type *ty, int sclass,
                              struct attrs *at)
{
    struct sym *s = n->gsym;
    if (s) {
        if (s->kind != kind)
            error_tok(tok(), "種類の違う再宣言", n->s);
        if (!compatible(unqual(s->ty), unqual(ty)))
            error_tok(tok(), "型の違う再宣言", n->s);
        /* 原型のある方・長さの決まった方を残す */
        if (ty->kind == TY_FUNC && ty->proto && !s->ty->proto)
            s->ty = ty;
        if (ty->kind == TY_ARRAY && s->ty->len < 0 && ty->len >= 0)
            s->ty = ty;
        if (sclass == SC_STATIC)
            s->sclass = SC_STATIC;
        else if (sclass == SC_NONE && s->sclass == SC_EXTERN)
            s->sclass = SC_NONE;
        merge_attrs(&s->at, at);
        /* 局所の有効範囲で宣言された extern を，ファイルの有効範囲から見えるようにする */
        if (level == 0 && n->ord != s) {
            s->shadow = n->ord;
            n->ord = s;
        }
        return s;
    }
    if (level == 0) {
        s = bind(n, kind, ty);
    } else {
        /* 局所の extern 宣言: 記号はファイルの有効範囲のものとして作る
         * (名前は呼ぶ側が別名の記号で局所に束ねる) */
        s = xmalloc(sizeof(struct sym));
        s->name = n;
        s->kind = kind;
        s->ty = ty;
        s->file = curfile;
        s->line = curline;
    }
    s->sclass = sclass;
    s->global = 1;
    s->at = *at;
    n->gsym = s;
    *gtail = s;
    gtail = &s->gnext;
    return s;
}

/* 局所の有効範囲に，ファイルの有効範囲の記号の別名を束ねる */
static void bind_alias(struct sym *g)
{
    struct sym *a;
    if (level == 0)
        return;
    a = bind(g->name, g->kind, g->ty);
    a->alias_of = g;
}

static void add_local(struct sym *s)
{
    struct sym **pp;
    if (!curfn)
        perr("関数の外の局所変数");
    for (pp = &curfn->locals; *pp; pp = &(*pp)->lnext)
        ;
    *pp = s;
}

static void funcdef(struct sym *s, struct type *t)
{
    struct param *p;
    struct sym **pt;
    if (s->defined && !(s->isinline && s->sclass == SC_EXTERN))
        error_tok(tok(), "関数の再定義", s->name->s);
    if (!t->proto && t->params == 0 && 0)
        perr("原型の無い関数の定義");
    s->ty = t;
    s->defined = 1;
    curfn = s;
    push_scope();
    pt = &s->params;
    for (p = t->params; p; p = p->next) {
        struct sym *ps;
        if (!p->name)
            perr("仮引数の名前が要る");
        ps = bind(p->name, SY_VAR, p->ty);
        ps->isparam = 1;
        *pt = ps;
        pt = &ps->pnext;
    }
    s->body = compound_stmt(0);
    pop_scope();
    curfn = 0;
}

static void declaration(int global, struct node ***tail)
{
    struct dspec d;
    if (iskw(K_STATIC_ASSERT)) {
        static_assert_decl();
        return;
    }
    declspec(&d, 1);
    if (!d.ty) {
        if (!d.sclass && !d.isinline)
            perr("宣言の型が要る");
        d.ty = &ty_int;     /* C89 の暗黙の int (記憶域の指定があるとき) */
    }
    if (accept_p(';'))
        return;
    for (;;) {
        struct name *n;
        struct type *t;
        struct attrs at = d.at;
        t = declarator(d.ty, &n);
        parse_attrs(&at);
        if (!n)
            perr("宣言の名前が要る");
        if (at.mode && t->kind != TY_FUNC && !d.at.mode)
            t = apply_mode(t, at.mode);
        if (at.aligned > talign(t) && t->kind != TY_FUNC) {
            t = copytype(t);
            t->align = at.aligned;
        }
        if (d.sclass == SC_TYPEDEF) {
            struct sym *s;
            if (n->ord && n->ord->kind == SY_TYPEDEF && n->ord->level == level
                && compatible(n->ord->ty, t)) {
                /* 同じ typedef の再宣言 (C11 / GNU) */
            } else {
                s = bind(n, SY_TYPEDEF, t);
                s->at = at;
            }
        } else if (t->kind == TY_FUNC) {
            int sc = d.sclass == SC_STATIC ? SC_STATIC : d.sclass == SC_EXTERN ? SC_EXTERN : SC_NONE;
            struct sym *s = global_sym(n, SY_FUNC, t, sc, &at);
            if (d.isinline) {
                s->isinline = 1;
                /* gnu89: extern inline は外部の定義を出さない */
                if (d.sclass == SC_EXTERN)
                    s->at.gnu_inline = 1;
            }
            if (at.gnu_inline && d.isinline && d.sclass == SC_EXTERN)
                s->at.gnu_inline = 1;
            bind_alias(s);
            if (global && ispunct_('{')) {
                funcdef(s, t);
                return;
            }
        } else if (global || d.sclass == SC_EXTERN) {
            int sc = d.sclass == SC_STATIC ? SC_STATIC : d.sclass == SC_EXTERN ? SC_EXTERN : SC_NONE;
            struct sym *s = global_sym(n, SY_VAR, t, sc, &at);
            bind_alias(s);
            if (accept_p('=')) {
                struct type *it = s->ty;
                if (s->hasinit)
                    error_tok(tok(), "変数の再定義", n->s);
                if (!global)
                    perr("局所の extern 宣言に初期化子がある");
                s->init = parse_initializer(&it, 1);
                s->ty = it;
                s->hasinit = 1;
                s->defined = 1;
            } else if (sc != SC_EXTERN)
                s->tentative = 1;
        } else if (d.sclass == SC_STATIC) {
            /* 静的な局所の変数: 名前は局所，実体は名前 + 番号のラベルを持つデータ */
            struct sym *s = bind(n, SY_VAR, t);
            char lab[256];
            s->sclass = SC_STATIC;
            s->isstaticlocal = 1;
            s->at = at;
            sprintf(lab, "%.200s.%d", n->s, nstaticlocal++);
            s->label = xstrdup(lab);
            *gtail = s;
            gtail = &s->gnext;
            if (accept_p('=')) {
                struct type *it = t;
                s->init = parse_initializer(&it, 1);
                s->ty = it;
                s->hasinit = 1;
            }
            s->defined = 1;
            s->tentative = !s->hasinit;
        } else {
            struct sym *s = bind(n, SY_VAR, t);
            s->sclass = d.sclass;
            s->at = at;
            add_local(s);
            if (accept_p('=')) {
                struct type *it = t;
                struct node *in = mkstmt(S_INIT);
                in->init = parse_initializer(&it, 0);
                s->ty = it;
                in->sym = s;
                **tail = in;
                *tail = &in->next;
            }
            if (tsize(s->ty) == 0 && s->ty->kind == TY_ARRAY && s->ty->len < 0)
                perr("大きさの決まらない局所の配列");
        }
        if (!accept_p(','))
            break;
    }
    expect_p(';');
}

/* 大域の asm("...") (そのまま出力へ写す) */
struct toplevel_asm {
    char *text;
    struct toplevel_asm *next;
};

static struct toplevel_asm *tlasm;
static struct toplevel_asm **tlasmtail = &tlasm;

static void translation_unit(void)
{
    while (tok()->kind != T_EOF) {
        if (accept_p(';'))
            continue;
        if (iskw(K_ASM)) {
            struct toplevel_asm *a = xmalloc(sizeof(struct toplevel_asm));
            advance();
            expect_p('(');
            a->text = string_arg();
            expect_p(')');
            expect_p(';');
            *tlasmtail = a;
            tlasmtail = &a->next;
            continue;
        }
        if (iskw(K_EXTENSION)) {
            advance();
            continue;
        }
        declaration(1, 0);
    }
}

/* ==== コード生成: 出力とレジスタ (docs/stage018-ncc.md 4.4) ==== */

static struct buf out;          /* 単位全体の出力 */
static struct buf fb;           /* 生成中の関数の本体 */
static int medany;              /* -mcmodel=medany */
static int nlabel;

static void emit(const char *fmt, ...)
{
    va_list ap;
    bput(&fb, "\t", 1);
    va_start(ap, fmt);
    bvprintf(&fb, fmt, ap);
    va_end(ap);
    bput(&fb, "\n", 1);
}

static void emit_label(int l)
{
    bprintf(&fb, ".L%d:\n", l);
}

static int newlabel(void)
{
    return nlabel++;
}

/* 式の一時値の溜まり: s1〜s11 (callee-saved なので呼出しをまたいでも残る) */
#define NPOOL 11
static const char *poolname[NPOOL] = {
    "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"
};
static int poolbusy[NPOOL];
static int poolever[NPOOL];

static int alloc_reg(void)
{
    int i;
    for (i = 0; i < NPOOL; i++)
        if (!poolbusy[i]) {
            poolbusy[i] = 1;
            poolever[i] = 1;
            return i;
        }
    fatal_at(curfile, curline, "式が深すぎる (一時値のレジスタが尽きた)", 0);
    return -1;
}

static void free_reg(int r)
{
    if (r >= 0)
        poolbusy[r] = 0;
}

static const char *R(int r)
{
    return poolname[r];
}

static const char *areg(int i)
{
    static const char *n[8] = { "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7" };
    return n[i & 7];
}

/* 値: 32 bit 以下は lo だけ，64 bit は lo / hi，構造体は lo に番地 */
struct val {
    int lo;
    int hi;
};

static void free_val(struct val v)
{
    free_reg(v.lo);
    free_reg(v.hi);
}

static struct val noval(void)
{
    struct val v;
    v.lo = -1;
    v.hi = -1;
    return v;
}

/* ==== フレーム ==== */

static i64 fhead;               /* s0 の下の固定部 (ra・s0・s1〜s11・可変長の退避) */
static i64 fdepth;              /* 局所と一時の領域の深さ */
static i64 maxout;              /* 呼出しのスタック引数の最大 */
static int retlabel;
static i64 retptr_off;          /* 構造体を返す関数: 隠れた引数の置き場 */
static i64 vastart_off;         /* va_start が指す s0 からの位置 */
static int fn_variadic;

static i64 alloc_slot(i64 size, int align)
{
    if (align < 4)
        align = 4;
    fdepth += size;
    fdepth = (fdepth + align - 1) / align * align;
    return -(fhead + fdepth);
}

/* base + off の番地で読み書きする。12 bit に入らなければ t6 で組む */
static void memop(const char *op, const char *reg, i64 off, const char *base)
{
    if (off >= -2048 && off <= 2047) {
        emit("%s %s, %L(%s)", op, reg, off, base);
        return;
    }
    emit("li t6, %L", off);
    emit("add t6, t6, %s", base);
    emit("%s %s, 0(t6)", op, reg);
}

static void addi_big(const char *rd, const char *rs, i64 v)
{
    if (v >= -2048 && v <= 2047)
        emit("addi %s, %s, %L", rd, rs, v);
    else {
        emit("li t6, %L", v);
        emit("add %s, %s, t6", rd, rs);
    }
}

static void mark_ref(struct sym *s);

/* 大域の記号の番地をレジスタに置く (code model に従う) */
static void load_symaddr(const char *rd, const char *label, i64 addend)
{
    char add[48];
    if (addend) {
        char num[32];
        sprintf(add, "%s%s", addend > 0 ? "+" : "", i64str(addend, num));
    } else
        add[0] = 0;
    if (medany)
        emit("lla %s, %s%s", rd, label, add);
    else {
        emit("lui %s, %%hi(%s%s)", rd, label, add);
        emit("addi %s, %s, %%lo(%s%s)", rd, rd, label, add);
    }
}

static const char *symlabel(struct sym *s)
{
    if (s->label)
        return s->label;
    return labelof(s);
}

/* ==== 読み書き ==== */

static const char *loadop(struct type *t)
{
    switch (tsize(t)) {
    case 1: return isunsignedt(t) ? "lbu" : "lb";
    case 2: return isunsignedt(t) ? "lhu" : "lh";
    }
    return "lw";
}

static const char *storeop(struct type *t)
{
    switch (tsize(t)) {
    case 1: return "sb";
    case 2: return "sh";
    }
    return "sw";
}

/* 番地 a (溜まりのレジスタ) の型 t の値を読む。a は解放する */
static struct val load(int a, struct type *t)
{
    struct val v = noval();
    if (t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_ARRAY
        || t->kind == TY_FUNC) {
        v.lo = a;
        return v;
    }
    if (is64(t)) {
        v.lo = alloc_reg();
        v.hi = alloc_reg();
        emit("lw %s, 0(%s)", R(v.lo), R(a));
        emit("lw %s, 4(%s)", R(v.hi), R(a));
        free_reg(a);
        return v;
    }
    v.lo = a;
    emit("%s %s, 0(%s)", loadop(t), R(a), R(a));
    return v;
}

/* 構造体の写し: src から dst へ size バイト (t0〜t3 を使う) */
static void copy_mem(const char *dst, const char *src, i64 size, int align)
{
    i64 i;
    if (align >= 4 && size % 4 == 0 && size <= 64) {
        for (i = 0; i < size; i += 4) {
            emit("lw t0, %L(%s)", i, src);
            emit("sw t0, %L(%s)", i, dst);
        }
        return;
    }
    if (size <= 16) {
        for (i = 0; i < size; i++) {
            emit("lbu t0, %L(%s)", i, src);
            emit("sb t0, %L(%s)", i, dst);
        }
        return;
    }
    {
        int l = newlabel();
        int w = align >= 4 && size % 4 == 0;
        emit("mv t1, %s", src);
        emit("mv t2, %s", dst);
        emit("li t3, %L", size);
        emit_label(l);
        emit("%s t0, 0(t1)", w ? "lw" : "lbu");
        emit("%s t0, 0(t2)", w ? "sw" : "sb");
        emit("addi t1, t1, %d", w ? 4 : 1);
        emit("addi t2, t2, %d", w ? 4 : 1);
        emit("addi t3, t3, %d", w ? -4 : -1);
        emit("bnez t3, .L%d", l);
    }
}

static void zero_mem(const char *dst, i64 size)
{
    i64 i;
    if (size <= 64) {
        for (i = 0; i + 4 <= size; i += 4)
            emit("sw zero, %L(%s)", i, dst);
        for (; i < size; i++)
            emit("sb zero, %L(%s)", i, dst);
        return;
    }
    {
        int l = newlabel();
        emit("mv t1, %s", dst);
        emit("li t3, %L", size);
        emit_label(l);
        emit("sb zero, 0(t1)");
        emit("addi t1, t1, 1");
        emit("addi t3, t3, -1");
        emit("bnez t3, .L%d", l);
    }
}

/* 番地 a に値 v (型 t) を書く。a は解放しない */
static void store(int a, struct val v, struct type *t)
{
    if (t->kind == TY_STRUCT || t->kind == TY_UNION) {
        copy_mem(R(a), R(v.lo), tsize(t), talign(t));
        return;
    }
    if (is64(t)) {
        emit("sw %s, 0(%s)", R(v.lo), R(a));
        emit("sw %s, 4(%s)", R(v.hi), R(a));
        return;
    }
    emit("%s %s, 0(%s)", storeop(t), R(v.lo), R(a));
}

/* ==== 型の変換 ==== */

/* 32 bit 以下の値を型 t に正規化する (符号拡張・ゼロ拡張) */
static void normalize(int r, struct type *t)
{
    switch (tsize(t)) {
    case 1:
        if (t->kind == TY_BOOL)
            break;
        if (isunsignedt(t))
            emit("andi %s, %s, 255", R(r), R(r));
        else {
            emit("slli %s, %s, 24", R(r), R(r));
            emit("srai %s, %s, 24", R(r), R(r));
        }
        break;
    case 2:
        emit("slli %s, %s, 16", R(r), R(r));
        emit("%s %s, %s, 16", isunsignedt(t) ? "srli" : "srai", R(r), R(r));
        break;
    }
}

static struct val convert(struct val v, struct type *from, struct type *to)
{
    if (to->kind == TY_VOID) {
        free_val(v);
        return noval();
    }
    if (to->kind == TY_STRUCT || to->kind == TY_UNION)
        return v;
    if (to->kind == TY_BOOL) {
        if (is64(from)) {
            emit("or %s, %s, %s", R(v.lo), R(v.lo), R(v.hi));
            free_reg(v.hi);
            v.hi = -1;
        }
        if (from->kind != TY_BOOL)
            emit("snez %s, %s", R(v.lo), R(v.lo));
        return v;
    }
    if (is64(to)) {
        if (is64(from))
            return v;
        v.hi = alloc_reg();
        if (isunsignedt(from))
            emit("li %s, 0", R(v.hi));
        else
            emit("srai %s, %s, 31", R(v.hi), R(v.lo));
        return v;
    }
    if (is64(from)) {
        free_reg(v.hi);
        v.hi = -1;
    }
    if (tsize(to) < 4 && (tsize(from) > tsize(to) || isunsignedt(from) != isunsignedt(to)
                          || is64(from)))
        normalize(v.lo, to);
    return v;
}

/* ==== 式の生成 ==== */

static struct val gen(struct node *n);
static int gen_addr(struct node *n);
static void gen_stmt(struct node *n);
static int reachable;

static struct sym *pending;             /* 出力を待つ static の並び */

/* 記号を参照した。static なら出力の対象に加える */
static void mark_ref(struct sym *s)
{
    if (s->alias_of)
        s = s->alias_of;
    if (s->referenced)
        return;
    s->referenced = 1;
    if (s->global || s->isstaticlocal) {
        s->fnext = pending;
        pending = s;
    }
}

static int gen_addr(struct node *n)
{
    int r;
    switch (n->op) {
    case N_VAR: {
        struct sym *s = n->sym;
        r = alloc_reg();
        if (s->global || s->isstaticlocal) {
            mark_ref(s);
            load_symaddr(R(r), symlabel(s), 0);
        } else if (s->byref)
            memop("lw", R(r), s->off, "s0");
        else
            addi_big(R(r), "s0", s->off);
        return r;
    }
    case N_DEREF: {
        struct val v = gen(n->a);
        return v.lo;
    }
    case N_MEMBER: {
        r = gen_addr(n->a);
        if (n->val)
            addi_big(R(r), R(r), n->val);
        return r;
    }
    case N_STR:
        r = alloc_reg();
        n->str->used = 1;
        {
            char lab[32];
            sprintf(lab, ".LC%d", n->str->id);
            load_symaddr(R(r), lab, 0);
        }
        return r;
    }
    if (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION) {
        /* 構造体の値 (呼出しの戻り値・代入・?: など) は番地で持つ */
        struct val v = gen(n);
        return v.lo;
    }
    fatal_at(n->file, n->line, "番地を取れない式", 0);
    return -1;
}

/* ビットフィールドの読み出し: 入れ物を読んで取り出す */
static struct val load_bitfield(int a, struct member *m, struct type *t)
{
    struct val v = noval();
    int sz = (int)tsize(m->ty);
    if (sz > 4 || m->bo + m->bw > 32)
        fatal_at(curfile, curline, "32 bit を超える入れ物のビットフィールドは扱わない", 0);
    v.lo = a;
    emit("%s %s, 0(%s)", sz == 1 ? "lbu" : sz == 2 ? "lhu" : "lw", R(a), R(a));
    if (m->bo + m->bw < 32)
        emit("slli %s, %s, %d", R(a), R(a), 32 - m->bo - m->bw);
    if (m->bw < 32)
        emit("%s %s, %s, %d", isunsignedt(m->ty) ? "srli" : "srai", R(a), R(a), 32 - m->bw);
    (void)t;
    return v;
}

/* ビットフィールドへの書込み。v は書く値 (解放しない) */
static void store_bitfield(int a, struct member *m, struct val v)
{
    int sz = (int)tsize(m->ty);
    const char *ld = sz == 1 ? "lbu" : sz == 2 ? "lhu" : "lw";
    const char *st = sz == 1 ? "sb" : sz == 2 ? "sh" : "sw";
    u64 mask = ((m->bw >= 32 ? 0xffffffffULL : ((1ULL << m->bw) - 1)) << m->bo) & 0xffffffffULL;
    if (sz > 4 || m->bo + m->bw > 32)
        fatal_at(curfile, curline, "32 bit を超える入れ物のビットフィールドは扱わない", 0);
    emit("%s t0, 0(%s)", ld, R(a));
    emit("li t1, %L", (i64)mask);
    emit("not t2, t1");
    emit("and t0, t0, t2");
    emit("slli t2, %s, %d", R(v.lo), m->bo);
    emit("and t2, t2, t1");
    emit("or t0, t0, t2");
    emit("%s t0, 0(%s)", st, R(a));
}

/* 書いた値を式の値として正規化する (ビットフィールドの幅で切る) */
static void trunc_bitfield(struct val v, struct member *m)
{
    if (m->bw >= 32)
        return;
    emit("slli %s, %s, %d", R(v.lo), R(v.lo), 32 - m->bw);
    emit("%s %s, %s, %d", isunsignedt(m->ty) ? "srli" : "srai", R(v.lo), R(v.lo), 32 - m->bw);
}

static int is_bitfield(struct node *n)
{
    return n->op == N_MEMBER && n->mem->bw >= 0;
}

/* ---- 64 bit の演算 ---- */

static void gen_shift64(int op, struct val a, int n)
{
    int lsmall = newlabel(), lend = newlabel();
    const char *lo = R(a.lo), *hi = R(a.hi), *sh = R(n);
    emit("andi t0, %s, 63", sh);
    emit("li t1, 32");
    emit("bltu t0, t1, .L%d", lsmall);
    emit("addi t0, t0, -32");
    if (op == N_SHL) {
        emit("sll %s, %s, t0", hi, lo);
        emit("li %s, 0", lo);
    } else if (op == 'u') {
        emit("srl %s, %s, t0", lo, hi);
        emit("li %s, 0", hi);
    } else {
        emit("sra %s, %s, t0", lo, hi);
        emit("srai %s, %s, 31", hi, hi);
    }
    emit("j .L%d", lend);
    emit_label(lsmall);
    emit("beqz t0, .L%d", lend);
    emit("sub t1, t1, t0");
    if (op == N_SHL) {
        emit("srl t2, %s, t1", lo);
        emit("sll %s, %s, t0", hi, hi);
        emit("or %s, %s, t2", hi, hi);
        emit("sll %s, %s, t0", lo, lo);
    } else {
        emit("sll t2, %s, t1", hi);
        emit("srl %s, %s, t0", lo, lo);
        emit("or %s, %s, t2", lo, lo);
        emit("%s %s, %s, t0", op == 'u' ? "srl" : "sra", hi, hi);
    }
    emit_label(lend);
}

/* a op b (どちらも 64 bit)。結果は a に置く。b は解放する */
static void gen_arith64(int op, struct type *t, struct val a, struct val b)
{
    const char *al = R(a.lo), *ah = R(a.hi), *bl = R(b.lo), *bh = R(b.hi);
    switch (op) {
    case N_ADD:
        emit("add %s, %s, %s", al, al, bl);
        emit("sltu t0, %s, %s", al, bl);
        emit("add %s, %s, %s", ah, ah, bh);
        emit("add %s, %s, t0", ah, ah);
        break;
    case N_SUB:
        emit("sltu t0, %s, %s", al, bl);
        emit("sub %s, %s, %s", al, al, bl);
        emit("sub %s, %s, %s", ah, ah, bh);
        emit("sub %s, %s, t0", ah, ah);
        break;
    case N_MUL:
        emit("mulhu t0, %s, %s", al, bl);
        emit("mul t1, %s, %s", al, bh);
        emit("mul t2, %s, %s", ah, bl);
        emit("mul %s, %s, %s", al, al, bl);
        emit("add t0, t0, t1");
        emit("add %s, t0, t2", ah);
        break;
    case N_AND: case N_OR: case N_XOR: {
        const char *o = op == N_AND ? "and" : op == N_OR ? "or" : "xor";
        emit("%s %s, %s, %s", o, al, al, bl);
        emit("%s %s, %s, %s", o, ah, ah, bh);
        break;
    }
    case N_DIV: case N_MOD: {
        const char *fn = op == N_DIV ? (isunsignedt(t) ? "__udivdi3" : "__divdi3")
            : (isunsignedt(t) ? "__umoddi3" : "__moddi3");
        emit("mv a0, %s", al);
        emit("mv a1, %s", ah);
        emit("mv a2, %s", bl);
        emit("mv a3, %s", bh);
        emit("call %s", fn);
        emit("mv %s, a0", al);
        emit("mv %s, a1", ah);
        break;
    }
    default:
        fatal_at(curfile, curline, "64 bit の演算を作れない", 0);
    }
    free_val(b);
}

/* 64 bit の比較。結果は 0 / 1 を新しいレジスタに */
static int gen_cmp64(int op, int uns, struct val a, struct val b)
{
    int r = alloc_reg();
    const char *al = R(a.lo), *ah = R(a.hi), *bl = R(b.lo), *bh = R(b.hi);
    const char *slt = uns ? "sltu" : "slt";
    switch (op) {
    case N_EQ: case N_NE:
        emit("xor t0, %s, %s", al, bl);
        emit("xor t1, %s, %s", ah, bh);
        emit("or t0, t0, t1");
        emit("%s %s, t0", op == N_EQ ? "seqz" : "snez", R(r));
        break;
    default: {
        /* x < y  <=>  hi(x) < hi(y) || (hi(x) == hi(y) && lo(x) <u lo(y)) */
        const char *xl = al, *xh = ah, *yl = bl, *yh = bh;
        int neg = 0;
        if (op == N_GT || op == N_LE) {
            xl = bl; xh = bh; yl = al; yh = ah;
        }
        if (op == N_LE || op == N_GE)
            neg = 1;
        emit("%s t0, %s, %s", slt, xh, yh);
        emit("xor t1, %s, %s", xh, yh);
        emit("seqz t1, t1");
        emit("sltu t2, %s, %s", xl, yl);
        emit("and t1, t1, t2");
        emit("or %s, t0, t1", R(r));
        if (neg)
            emit("xori %s, %s, 1", R(r), R(r));
        break;
    }
    }
    free_val(a);
    free_val(b);
    return r;
}

/* ---- 32 bit 以下の 2 項演算 ---- */

static void gen_arith32(int op, struct type *t, int a, int b)
{
    const char *o = 0;
    int u = isunsignedt(t);
    switch (op) {
    case N_ADD: o = "add"; break;
    case N_SUB: o = "sub"; break;
    case N_MUL: o = "mul"; break;
    case N_DIV: o = u ? "divu" : "div"; break;
    case N_MOD: o = u ? "remu" : "rem"; break;
    case N_AND: o = "and"; break;
    case N_OR: o = "or"; break;
    case N_XOR: o = "xor"; break;
    case N_SHL: o = "sll"; break;
    case N_SHR: o = u ? "srl" : "sra"; break;
    }
    emit("%s %s, %s, %s", o, R(a), R(a), R(b));
    if (tsize(t) < 4)
        normalize(a, t);
}

static int gen_cmp32(int op, int uns, int a, int b)
{
    const char *slt = uns ? "sltu" : "slt";
    switch (op) {
    case N_EQ:
        emit("xor %s, %s, %s", R(a), R(a), R(b));
        emit("seqz %s, %s", R(a), R(a));
        break;
    case N_NE:
        emit("xor %s, %s, %s", R(a), R(a), R(b));
        emit("snez %s, %s", R(a), R(a));
        break;
    case N_LT:
        emit("%s %s, %s, %s", slt, R(a), R(a), R(b));
        break;
    case N_GT:
        emit("%s %s, %s, %s", slt, R(a), R(b), R(a));
        break;
    case N_LE:
        emit("%s %s, %s, %s", slt, R(a), R(b), R(a));
        emit("xori %s, %s, 1", R(a), R(a));
        break;
    case N_GE:
        emit("%s %s, %s, %s", slt, R(a), R(a), R(b));
        emit("xori %s, %s, 1", R(a), R(a));
        break;
    }
    free_reg(b);
    return a;
}

/* 値 v (型 t) が 0 でないかを 1 つのレジスタにする */
static int truth(struct val v, struct type *t)
{
    if (is64(t)) {
        emit("or %s, %s, %s", R(v.lo), R(v.lo), R(v.hi));
        free_reg(v.hi);
    }
    return v.lo;
}

/* 2 項演算 op を型 t で。a に結果，b は解放 */
static struct val binary(int op, struct type *t, struct val a, struct val b)
{
    if (is64(t)) {
        if (op == N_SHL || op == N_SHR) {
            gen_shift64(op == N_SHL ? N_SHL : (isunsignedt(t) ? 'u' : 's'), a, b.lo);
            free_val(b);
            return a;
        }
        gen_arith64(op, t, a, b);
        return a;
    }
    gen_arith32(op, t, a.lo, b.lo);
    free_val(b);
    return a;
}

/* ---- 呼出し (docs/stage018-ncc.md 4.5) ---- */

/* 引数 1 つの渡し方: 語の数 (1 / 2)，番地で渡すか，2 語の整列を要るか */
struct argcls {
    int words;
    int byref;
    int align8;
};

static struct argcls classify(struct type *t)
{
    struct argcls c;
    c.words = 1;
    c.byref = 0;
    c.align8 = 0;
    if (t->kind == TY_STRUCT || t->kind == TY_UNION) {
        i64 sz = tsize(t);
        if (sz > 8) {
            c.byref = 1;
            return c;
        }
        c.words = sz > 4 ? 2 : 1;
        c.align8 = talign(t) >= 8 && c.words == 2;
        return c;
    }
    if (is64(t)) {
        c.words = 2;
        c.align8 = 1;
    }
    return c;
}

/* 番地 a から構造体の語 (size が 4 未満でも 1 語) をレジスタ rd へ読む */
static void load_word(const char *rd, const char *a, i64 off, i64 size, int align)
{
    if (align >= 4 || (align >= 2 && size >= 4 && 0)) {
        emit("lw %s, %L(%s)", rd, off, a);
        return;
    }
    {
        /* 整列の足りない構造体はバイトで組む */
        i64 i, n = size - off < 4 ? size - off : 4;
        emit("li %s, 0", rd);
        for (i = 0; i < n; i++) {
            emit("lbu t5, %L(%s)", off + i, a);
            if (i)
                emit("slli t5, t5, %d", (int)(i * 8));
            emit("or %s, %s, t5", rd, rd);
        }
    }
}

static int is_direct_call(struct node *f, struct sym **s)
{
    if (f->op == N_ADDR && f->a->op == N_VAR && f->a->ty->kind == TY_FUNC) {
        *s = f->a->sym;
        return 1;
    }
    return 0;
}

static struct val gen_call(struct node *n)
{
    struct type *ft = n->a->ty->base;
    struct type *rt = ft->base;
    struct node *arg;
    struct sym *direct = 0;
    struct val fv = noval(), res = noval();
    int nargs = 0, i, hidden, regi, spilled = 0;
    i64 nwords = 0, bufoff, stk = 0, retslot = 0;
    struct type **atys;
    i64 *woff;
    int *aw;
    struct argcls rc = classify(rt);
    int nparams = 0;
    struct param *p;

    for (p = ft->params; p; p = p->next)
        nparams++;
    hidden = (rt->kind == TY_STRUCT || rt->kind == TY_UNION) && rc.byref;
    if (!is_direct_call(n->a, &direct))
        fv = gen(n->a);
    else
        mark_ref(direct);
    for (arg = n->b; arg; arg = arg->next)
        nargs++;
    atys = xmalloc((long)(nargs + 1) * (long)sizeof(struct type *));
    woff = xmalloc((long)(nargs + 1) * (long)sizeof(i64));
    aw = xmalloc((long)(nargs + 1) * (long)sizeof(int));
    /* 引数を評価して，呼出しごとの領域 (フレームの中) に語で置く */
    for (arg = n->b, i = 0; arg; arg = arg->next, i++) {
        struct argcls c = classify(arg->ty);
        atys[i] = arg->ty;
        aw[i] = c.words;
        nwords += c.words;
    }
    bufoff = alloc_slot(nwords * 4 + 8, 8);
    {
        i64 w = 0;
        for (arg = n->b, i = 0; arg; arg = arg->next, i++) {
            struct argcls c = classify(arg->ty);
            struct val v = gen(arg);
            woff[i] = bufoff + w * 4;
            if (arg->ty->kind == TY_STRUCT || arg->ty->kind == TY_UNION) {
                i64 sz = tsize(arg->ty);
                if (c.byref) {
                    /* 呼ぶ側の写しへの番地を渡す */
                    i64 cp = alloc_slot(sz, talign(arg->ty) > 4 ? talign(arg->ty) : 4);
                    addi_big("t4", "s0", cp);
                    copy_mem("t4", R(v.lo), sz, talign(arg->ty));
                    memop("sw", "t4", woff[i], "s0");
                } else {
                    load_word("t4", R(v.lo), 0, sz, talign(arg->ty));
                    memop("sw", "t4", woff[i], "s0");
                    if (c.words == 2) {
                        load_word("t4", R(v.lo), 4, sz, talign(arg->ty));
                        memop("sw", "t4", woff[i] + 4, "s0");
                    }
                }
            } else {
                memop("sw", R(v.lo), woff[i], "s0");
                if (c.words == 2)
                    memop("sw", R(v.hi), woff[i] + 4, "s0");
            }
            free_val(v);
            w += c.words;
        }
    }
    /* 語をレジスタ a0〜a7 とスタックに割り付ける (psABI の整数の規約) */
    regi = hidden ? 1 : 0;
    {
        int *wreg = xmalloc((long)(nwords + 1) * (long)sizeof(int));
        i64 *wstk = xmalloc((long)(nwords + 1) * (long)sizeof(i64));
        i64 k = 0;
        for (i = 0; i < nargs; i++) {
            struct argcls c = classify(atys[i]);
            int isvar = i >= nparams && ft->variadic;
            if (c.words == 1) {
                if (!spilled && regi < 8) {
                    wreg[k] = regi++;
                } else {
                    wreg[k] = -1;
                    wstk[k] = stk;
                    stk += 4;
                }
                k++;
                continue;
            }
            if (isvar && c.align8) {
                /* 可変長の 2 語の整列: 偶数番から。無ければスタックへ移り，以降も
                 * すべてスタック */
                if (regi & 1)
                    regi++;
                if (spilled || regi > 6) {
                    spilled = 1;
                    regi = 8;
                }
            }
            if (!spilled && regi <= 6) {
                wreg[k] = regi++;
                wreg[k + 1] = regi++;
            } else if (!spilled && regi == 7) {
                wreg[k] = 7;
                regi = 8;
                wreg[k + 1] = -1;
                wstk[k + 1] = stk;
                stk += 4;
            } else {
                if (c.align8)
                    stk = (stk + 7) / 8 * 8;
                wreg[k] = -1;
                wstk[k] = stk;
                wreg[k + 1] = -1;
                wstk[k + 1] = stk + 4;
                stk += 8;
            }
            k += 2;
        }
        if (stk > maxout)
            maxout = stk;
        /* スタックの語を先に，レジスタの語を後に置く */
        for (k = 0, i = 0; i < nargs; i++) {
            int j;
            for (j = 0; j < aw[i]; j++, k++) {
                if (wreg[k] < 0) {
                    memop("lw", "t0", woff[i] + j * 4, "s0");
                    memop("sw", "t0", wstk[k], "sp");
                }
            }
        }
        for (k = 0, i = 0; i < nargs; i++) {
            int j;
            for (j = 0; j < aw[i]; j++, k++) {
                if (wreg[k] >= 0)
                    memop("lw", areg(wreg[k]), woff[i] + j * 4, "s0");
            }
        }
        free(wreg);
        free(wstk);
    }
    if (hidden) {
        retslot = alloc_slot(tsize(rt), talign(rt) > 4 ? talign(rt) : 4);
        addi_big("a0", "s0", retslot);
    }
    if (direct)
        emit("call %s", symlabel(direct));
    else {
        emit("jalr %s", R(fv.lo));
        free_val(fv);
    }
    free(atys);
    free(woff);
    free(aw);
    /* 戻り値 */
    if (rt->kind == TY_VOID)
        return noval();
    if (rt->kind == TY_STRUCT || rt->kind == TY_UNION) {
        res.lo = alloc_reg();
        if (hidden) {
            addi_big(R(res.lo), "s0", retslot);
        } else {
            i64 sl = alloc_slot(8, 8);
            memop("sw", "a0", sl, "s0");
            memop("sw", "a1", sl + 4, "s0");
            addi_big(R(res.lo), "s0", sl);
        }
        return res;
    }
    res.lo = alloc_reg();
    emit("mv %s, a0", R(res.lo));
    if (is64(rt)) {
        res.hi = alloc_reg();
        emit("mv %s, a1", R(res.hi));
    }
    return res;
}

/* ---- 代入 ---- */

static struct val gen_assign(struct node *n)
{
    struct val v;
    int a;
    if (is_bitfield(n->a)) {
        v = gen(n->b);
        a = gen_addr(n->a->a);
        if (n->a->val)
            addi_big(R(a), R(a), n->a->val);
        store_bitfield(a, n->a->mem, v);
        free_reg(a);
        trunc_bitfield(v, n->a->mem);
        return v;
    }
    v = gen(n->b);
    a = gen_addr(n->a);
    store(a, v, n->a->ty);
    if (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION) {
        /* 構造体の代入の値は左辺 (の写し) */
        free_val(v);
        v.lo = a;
        v.hi = -1;
        return v;
    }
    free_reg(a);
    return v;
}

/* 複合代入と ++ / -- の共通部: 番地を 1 度だけ求め，読み，計算し，書く */
static struct val gen_rmw(struct node *lhs, int op, struct type *ctype, struct node *rhs,
                          i64 step, int post)
{
    struct type *lt = lhs->ty;
    int a, bf = is_bitfield(lhs);
    struct val old, cur, b;
    if (bf) {
        a = gen_addr(lhs->a);
        if (lhs->val)
            addi_big(R(a), R(a), lhs->val);
    } else
        a = gen_addr(lhs);
    {
        int c = alloc_reg();
        emit("mv %s, %s", R(c), R(a));
        old = bf ? load_bitfield(c, lhs->mem, lt) : load(c, lt);
    }
    if (rhs) {
        cur = convert(old, lt, ctype);
        b = gen(rhs);
        if (isptr(ctype))
            cur = binary(op, &ty_int, cur, b);
        else
            cur = binary(op, ctype, cur, b);
        cur = convert(cur, ctype, lt);
    } else {
        /* ++ / --: 古い値を残して新しい値を作る */
        cur.lo = alloc_reg();
        cur.hi = -1;
        if (is64(lt)) {
            struct val one;
            cur.hi = alloc_reg();
            emit("mv %s, %s", R(cur.lo), R(old.lo));
            emit("mv %s, %s", R(cur.hi), R(old.hi));
            one.lo = alloc_reg();
            one.hi = alloc_reg();
            emit("li %s, %L", R(one.lo), step);
            emit("li %s, %d", R(one.hi), step < 0 ? -1 : 0);
            gen_arith64(N_ADD, lt, cur, one);
        } else {
            addi_big(R(cur.lo), R(old.lo), step);
            if (lt->kind == TY_BOOL)
                emit("snez %s, %s", R(cur.lo), R(cur.lo));
            else if (tsize(lt) < 4)
                normalize(cur.lo, lt);
        }
    }
    if (bf) {
        store_bitfield(a, lhs->mem, cur);
        trunc_bitfield(cur, lhs->mem);
    } else
        store(a, cur, lt);
    free_reg(a);
    if (post) {
        free_val(cur);
        return old;
    }
    if (rhs == 0)
        free_val(old);
    return cur;
}

/* va_arg: ap の指す所から型 t の値を読み，ap を進める */
static struct val gen_vaarg(struct node *n)
{
    struct type *t = n->ty;
    struct argcls c = classify(t);
    int a = gen_addr(n->a), p = alloc_reg();
    struct val v;
    emit("lw %s, 0(%s)", R(p), R(a));
    if (c.align8) {
        emit("addi %s, %s, 7", R(p), R(p));
        emit("andi %s, %s, -8", R(p), R(p));
    }
    emit("addi t0, %s, %d", R(p), c.byref ? 4 : c.words * 4);
    emit("sw t0, 0(%s)", R(a));
    free_reg(a);
    if (c.byref)
        emit("lw %s, 0(%s)", R(p), R(p));
    v = load(p, t);
    return v;
}

static struct val gen_stmtexpr(struct node *n)
{
    struct node *s;
    struct val v = noval();
    for (s = n->a->a; s; s = s->next) {
        if (!s->next && s->op == S_EXPR && n->ty->kind != TY_VOID) {
            if (!reachable)
                fatal_at(s->file, s->line, "文式の値が到達しない", 0);
            v = gen(s->a);
            break;
        }
        gen_stmt(s);
    }
    return v;
}

static struct val gen(struct node *n)
{
    struct val v = noval(), a, b;
    curfile = n->file;
    curline = n->line;
    switch (n->op) {
    case N_NUM:
        v.lo = alloc_reg();
        emit("li %s, %L", R(v.lo), is64(n->ty) ? (i64)((u64)n->val & 0xffffffffULL) : n->val);
        if (is64(n->ty)) {
            v.hi = alloc_reg();
            emit("li %s, %L", R(v.hi), (i64)(((u64)n->val >> 32) & 0xffffffffULL));
        }
        return v;
    case N_STR:
    case N_VAR:
        if (n->ty->kind == TY_FUNC || n->ty->kind == TY_ARRAY) {
            v.lo = gen_addr(n);
            return v;
        }
        return load(gen_addr(n), n->ty);
    case N_DEREF:
        return load(gen_addr(n), n->ty);
    case N_MEMBER:
        if (is_bitfield(n)) {
            int r = gen_addr(n->a);
            if (n->val)
                addi_big(R(r), R(r), n->val);
            return load_bitfield(r, n->mem, n->ty);
        }
        return load(gen_addr(n), n->ty);
    case N_ADDR:
        v.lo = gen_addr(n->a);
        return v;
    case N_CAST:
        a = gen(n->a);
        return convert(a, n->a->ty, n->ty);
    case N_ADD: case N_SUB: case N_MUL: case N_DIV: case N_MOD: case N_AND:
    case N_OR: case N_XOR: case N_SHL: case N_SHR:
        a = gen(n->a);
        b = gen(n->b);
        return binary(n->op, isptr(n->ty) ? &ty_int : n->ty, a, b);
    case N_PSUB: {
        a = gen(n->a);
        b = gen(n->b);
        emit("sub %s, %s, %s", R(a.lo), R(a.lo), R(b.lo));
        free_val(b);
        if (n->val > 1) {
            i64 s = n->val;
            int k = 0;
            while ((1LL << k) < s)
                k++;
            if ((1LL << k) == s)
                emit("srai %s, %s, %d", R(a.lo), R(a.lo), k);
            else {
                emit("li t0, %L", s);
                emit("div %s, %s, t0", R(a.lo), R(a.lo));
            }
        }
        return a;
    }
    case N_EQ: case N_NE: case N_LT: case N_LE: case N_GT: case N_GE: {
        struct type *t = n->a->ty;
        a = gen(n->a);
        b = gen(n->b);
        if (is64(t))
            v.lo = gen_cmp64(n->op, isunsignedt(t), a, b);
        else
            v.lo = gen_cmp32(n->op, isunsignedt(t) || isptr(t), a.lo, b.lo);
        return v;
    }
    case N_NOT:
        a = gen(n->a);
        v.lo = truth(a, n->a->ty);
        emit("seqz %s, %s", R(v.lo), R(v.lo));
        return v;
    case N_BNOT:
        a = gen(n->a);
        emit("not %s, %s", R(a.lo), R(a.lo));
        if (a.hi >= 0)
            emit("not %s, %s", R(a.hi), R(a.hi));
        return a;
    case N_LAND:
    case N_LOR: {
        int lend = newlabel(), r;
        a = gen(n->a);
        r = truth(a, n->a->ty);
        emit("snez %s, %s", R(r), R(r));
        emit("%s %s, .L%d", n->op == N_LAND ? "beqz" : "bnez", R(r), lend);
        free_reg(r);
        b = gen(n->b);
        {
            int r2 = truth(b, n->b->ty);
            if (r2 != r)
                emit("snez %s, %s", R(r), R(r2));
            else
                emit("snez %s, %s", R(r), R(r));
            if (r2 != r)
                free_reg(r2);
        }
        poolbusy[r] = 1;
        emit_label(lend);
        v.lo = r;
        return v;
    }
    case N_COND: {
        int lelse = newlabel(), lend = newlabel(), c;
        struct val res = noval();
        int big = is64(n->ty);
        a = gen(n->a);
        c = truth(a, n->a->ty);
        emit("beqz %s, .L%d", R(c), lelse);
        free_reg(c);
        if (n->ty->kind != TY_VOID) {
            res.lo = alloc_reg();
            if (big)
                res.hi = alloc_reg();
        }
        b = gen(n->b);
        if (n->ty->kind != TY_VOID) {
            emit("mv %s, %s", R(res.lo), R(b.lo));
            if (big)
                emit("mv %s, %s", R(res.hi), R(b.hi));
        }
        free_val(b);
        emit("j .L%d", lend);
        emit_label(lelse);
        b = gen(n->c);
        if (n->ty->kind != TY_VOID) {
            emit("mv %s, %s", R(res.lo), R(b.lo));
            if (big)
                emit("mv %s, %s", R(res.hi), R(b.hi));
        }
        free_val(b);
        emit_label(lend);
        return res;
    }
    case N_COMMA:
        a = gen(n->a);
        free_val(a);
        return gen(n->b);
    case N_ASSIGN:
        return gen_assign(n);
    case N_OPASSIGN:
        return gen_rmw(n->a, (int)n->val, n->ctype, n->b, 0, 0);
    case N_PREINC:
        return gen_rmw(n->a, 0, 0, 0, n->val, 0);
    case N_POSTINC:
        return gen_rmw(n->a, 0, 0, 0, n->val, 1);
    case N_CALL:
        return gen_call(n);
    case N_VASTART: {
        int r = gen_addr(n->a);
        addi_big("t0", "s0", vastart_off);
        emit("sw t0, 0(%s)", R(r));
        free_reg(r);
        return noval();
    }
    case N_VAARG:
        return gen_vaarg(n);
    case N_STMTEXPR:
        return gen_stmtexpr(n);
    case N_BUILTIN:
        if (n->val == 1)
            emit("ebreak");
        return noval();
    }
    fatal_at(n->file, n->line, "生成できない式", 0);
    return v;
}

/* ==== 文の生成 ==== */

struct loopctx {
    int brk;
    int cont;
    struct loopctx *up;
};

static struct loopctx *curloop;

/* goto のラベル (名前ごとに番号) */
struct glabel {
    struct name *name;
    int label;
    struct glabel *next;
};

static struct glabel *glabels;

static int goto_label(struct name *n)
{
    struct glabel *g;
    for (g = glabels; g; g = g->next)
        if (g->name == n)
            return g->label;
    g = xmalloc(sizeof(struct glabel));
    g->name = n;
    g->label = newlabel();
    g->next = glabels;
    glabels = g;
    return g->label;
}

static void jump(int l)
{
    if (reachable)
        emit("j .L%d", l);
    reachable = 0;
}

static void place(int l)
{
    emit_label(l);
    reachable = 1;
}

/* 条件が偽なら lfalse へ跳ぶ */
static void branch_false(struct node *c, int lfalse)
{
    struct val v = gen(c);
    int r = truth(v, c->ty);
    emit("beqz %s, .L%d", R(r), lfalse);
    free_reg(r);
}

static void gen_init(struct node *n)
{
    struct sym *s = n->sym;
    struct ient *e;
    int a;
    if (isaggr(s->ty)) {
        a = alloc_reg();
        addi_big(R(a), "s0", s->off);
        zero_mem(R(a), tsize(s->ty));
        free_reg(a);
    }
    for (e = n->init; e; e = e->next) {
        if (e->expr->op == N_STR && e->ty->kind == TY_ARRAY) {
            /* 文字の配列を文字列で初期化する */
            struct strlit *l = e->expr->str;
            i64 len = l->len + 1, i;
            if (e->ty->len >= 0 && len > e->ty->len)
                len = e->ty->len;
            a = alloc_reg();
            addi_big(R(a), "s0", s->off + e->off);
            for (i = 0; i < len; i++) {
                int ch = i < l->len ? (unsigned char)l->s[i] : 0;
                if (ch) {
                    emit("li t0, %d", ch);
                    emit("sb t0, %L(%s)", i, R(a));
                } else if (!isaggr(s->ty) || 1)
                    emit("sb zero, %L(%s)", i, R(a));
            }
            free_reg(a);
            continue;
        }
        {
            struct val v = gen(e->expr);
            a = alloc_reg();
            addi_big(R(a), "s0", s->off + e->off);
            if (e->bf)
                store_bitfield(a, e->bf, v);
            else
                store(a, v, e->ty);
            free_reg(a);
            free_val(v);
        }
    }
}

/* 枝に到達しない時でも，中のラベルは到達しうる (goto の行き先) */
static void gen_stmt(struct node *n)
{
    struct node *s;
    curfile = n->file;
    curline = n->line;
    switch (n->op) {
    case S_BLOCK:
        for (s = n->a; s; s = s->next)
            gen_stmt(s);
        return;
    case S_NULL:
        return;
    case S_EXPR:
        if (reachable)
            free_val(gen(n->a));
        return;
    case S_INIT:
        if (reachable)
            gen_init(n);
        return;
    case S_IF: {
        i64 c;
        if (eval_int(n->a, &c)) {
            /* 定数の条件: 選ばれない側は到達しない (中のラベルからだけ入れる) */
            int save = reachable, lend = newlabel();
            if (c) {
                gen_stmt(n->b);
                if (n->c) {
                    jump(lend);
                    gen_stmt(n->c);
                }
            } else {
                reachable = 0;
                gen_stmt(n->b);
                if (reachable)
                    jump(lend);
                reachable = save;
                if (n->c)
                    gen_stmt(n->c);
            }
            emit_label(lend);
            return;
        }
        {
            int lelse = newlabel(), lend = newlabel();
            if (reachable)
                branch_false(n->a, lelse);
            gen_stmt(n->b);
            if (n->c) {
                jump(lend);
                place(lelse);
                gen_stmt(n->c);
                place(lend);
            } else {
                place(lelse);
                emit_label(lend);
            }
        }
        return;
    }
    case S_WHILE:
    case S_FOR: {
        struct loopctx lc;
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        struct node *cond = n->op == S_WHILE ? n->a : n->b;
        struct node *body = n->op == S_WHILE ? n->b : n->d;
        i64 c = 1;
        int constcond = !cond || eval_int(cond, &c);
        if (n->op == S_FOR && n->a)
            gen_stmt(n->a);
        lc.brk = lend;
        lc.cont = lcont;
        lc.up = curloop;
        curloop = &lc;
        if (constcond && !c) {
            /* while (0): 本体は到達しない */
            int save = reachable;
            reachable = 0;
            gen_stmt(body);
            curloop = lc.up;
            place(lcont);
            emit_label(lend);
            reachable = save;
            return;
        }
        place(ltop);
        if (!constcond)
            branch_false(cond, lend);
        gen_stmt(body);
        place(lcont);
        if (n->op == S_FOR && n->c && reachable)
            free_val(gen(n->c));
        jump(ltop);
        curloop = lc.up;
        place(lend);
        return;
    }
    case S_DO: {
        struct loopctx lc;
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        i64 c;
        lc.brk = lend;
        lc.cont = lcont;
        lc.up = curloop;
        curloop = &lc;
        place(ltop);
        gen_stmt(n->b);
        place(lcont);
        if (eval_int(n->a, &c)) {
            if (c)
                jump(ltop);
        } else if (reachable) {
            struct val v = gen(n->a);
            int r = truth(v, n->a->ty);
            emit("bnez %s, .L%d", R(r), ltop);
            free_reg(r);
        }
        curloop = lc.up;
        place(lend);
        return;
    }
    case S_SWITCH: {
        struct loopctx lc;
        struct node *c, *def = 0;
        int lend = newlabel();
        i64 sv;
        int isconst = eval_int(n->a, &sv);
        for (c = n->cases; c; c = c->nextcase) {
            c->label = newlabel();
            c->targeted = !isconst;
        }
        if (reachable) {
            if (isconst) {
                /* 定数の選択式: 合う case へ直に跳ぶ。他の case は到達しない */
                struct node *hit = 0;
                for (c = n->cases; c; c = c->nextcase) {
                    if (c->op == S_DEFAULT)
                        def = c;
                    else if (sv >= c->val && sv <= c->val2)
                        hit = c;
                }
                if (!hit)
                    hit = def;
                if (hit)
                    hit->targeted = 1;
                jump(hit ? hit->label : lend);
            } else {
                struct val v = gen(n->a);
                int big = is64(n->a->ty);
                for (c = n->cases; c; c = c->nextcase) {
                    if (c->op == S_DEFAULT) {
                        def = c;
                        continue;
                    }
                    if (big) {
                        emit("li t0, %L", (i64)((u64)c->val & 0xffffffffULL));
                        emit("li t1, %L", (i64)(((u64)c->val >> 32) & 0xffffffffULL));
                        if (c->val != c->val2)
                            fatal_at(c->file, c->line, "64 bit の case の範囲は扱わない", 0);
                        emit("xor t0, t0, %s", R(v.lo));
                        emit("xor t1, t1, %s", R(v.hi));
                        emit("or t0, t0, t1");
                        emit("beqz t0, .L%d", c->label);
                    } else if (c->val == c->val2) {
                        emit("li t0, %L", c->val);
                        emit("beq %s, t0, .L%d", R(v.lo), c->label);
                    } else {
                        emit("li t0, %L", c->val);
                        emit("sub t0, %s, t0", R(v.lo));
                        emit("li t1, %L", (i64)((u64)(c->val2 - c->val) & 0xffffffffULL));
                        emit("bleu t0, t1, .L%d", c->label);
                    }
                }
                free_val(v);
                jump(def ? def->label : lend);
            }
        }
        reachable = 0;
        lc.brk = lend;
        lc.cont = curloop ? curloop->cont : -1;
        lc.up = curloop;
        curloop = &lc;
        gen_stmt(n->b);
        curloop = lc.up;
        place(lend);
        return;
    }
    case S_CASE:
    case S_DEFAULT:
        /* 定数の選択式で選ばれない case は跳んでこないが，前の case から
         * 落ちてきた場合は到達する (reachable はそのまま) */
        emit_label(n->label);
        if (n->targeted)
            reachable = 1;
        gen_stmt(n->a);
        return;
    case S_BREAK: {
        struct loopctx *l = curloop;
        if (!l)
            fatal_at(n->file, n->line, "break の行き先が無い", 0);
        jump(l->brk);
        return;
    }
    case S_CONTINUE: {
        struct loopctx *l = curloop;
        while (l && l->cont < 0)
            l = l->up;
        if (!l)
            fatal_at(n->file, n->line, "continue の行き先が無い", 0);
        jump(l->cont);
        return;
    }
    case S_RETURN:
        if (reachable && n->a) {
            struct type *rt = curfn->ty->base;
            struct val v = gen(n->a);
            if (rt->kind == TY_STRUCT || rt->kind == TY_UNION) {
                struct argcls c = classify(rt);
                if (c.byref) {
                    memop("lw", "t4", retptr_off, "s0");
                    copy_mem("t4", R(v.lo), tsize(rt), talign(rt));
                } else {
                    load_word("a0", R(v.lo), 0, tsize(rt), talign(rt));
                    if (c.words == 2)
                        load_word("a1", R(v.lo), 4, tsize(rt), talign(rt));
                }
            } else if (rt->kind != TY_VOID) {
                emit("mv a0, %s", R(v.lo));
                if (v.hi >= 0)
                    emit("mv a1, %s", R(v.hi));
            }
            free_val(v);
        }
        jump(retlabel);
        return;
    case S_GOTO:
        jump(goto_label(n->lname));
        return;
    case S_LABEL:
        place(goto_label(n->lname));
        gen_stmt(n->a);
        return;
    }
    fatal_at(n->file, n->line, "生成できない文", 0);
}

/* ==== 関数の出力 ==== */

static void gen_function(struct sym *f)
{
    struct sym *s;
    struct argcls rc = classify(f->ty->base);
    int regi = 0, i;
    i64 stk = 0, frame, V, first;
    struct buf body;
    char num[32];

    fb.n = 0;
    if (fb.p)
        fb.p[0] = 0;
    memset(poolbusy, 0, sizeof poolbusy);
    memset(poolever, 0, sizeof poolever);
    fdepth = 0;
    maxout = 0;
    glabels = 0;
    curloop = 0;
    reachable = 1;
    curfn = f;
    fn_variadic = f->ty->variadic;
    V = fn_variadic ? 32 : 0;
    fhead = V + 8 + 4 * NPOOL;
    retlabel = newlabel();

    /* 仮引数を受け取る (呼ぶ側の割付けと同じ規則。可変長の整列は固定の
     * 引数には無い) */
    if ((f->ty->base->kind == TY_STRUCT || f->ty->base->kind == TY_UNION) && rc.byref) {
        retptr_off = alloc_slot(4, 4);
        memop("sw", "a0", retptr_off, "s0");
        regi = 1;
    }
    for (s = f->params; s; s = s->pnext) {
        struct argcls c = classify(s->ty);
        const char *rn, *rn2;
        if (c.byref || c.words == 1) {
            if (regi < 8) {
                rn = areg(regi++);
                s->off = alloc_slot(c.byref ? 4 : (tsize(s->ty) > 4 ? tsize(s->ty) : 4),
                                    talign(s->ty) > 4 ? talign(s->ty) : 4);
                memop("sw", rn, s->off, "s0");
            } else {
                s->off = stk;
                stk += 4;
            }
            s->byref = c.byref;
            continue;
        }
        if (regi <= 6) {
            rn = areg(regi);
            rn2 = areg(regi + 1);
            regi += 2;
            s->off = alloc_slot(8, talign(s->ty) > 4 ? talign(s->ty) : 4);
            memop("sw", rn, s->off, "s0");
            memop("sw", rn2, s->off + 4, "s0");
        } else if (regi == 7) {
            regi = 8;
            s->off = alloc_slot(8, talign(s->ty) > 4 ? talign(s->ty) : 4);
            memop("sw", "a7", s->off, "s0");
            memop("lw", "t0", stk, "s0");
            memop("sw", "t0", s->off + 4, "s0");
            stk += 4;
        } else {
            if (c.align8)
                stk = (stk + 7) / 8 * 8;
            s->off = stk;
            stk += 8;
        }
    }
    vastart_off = regi < 8 ? -32 + 4 * regi : stk;
    if (fn_variadic)
        for (i = regi; i < 8; i++)
            memop("sw", areg(i), -32 + 4 * i, "s0");
    for (s = f->locals; s; s = s->lnext) {
        i64 sz = tsize(s->ty);
        s->off = alloc_slot(sz > 0 ? sz : 1, talign(s->ty));
    }
    gen_stmt(f->body);
    place(retlabel);

    /* ここまでの本体を取っておき，プロローグを前に付けて出す */
    body = fb;
    memset(&fb, 0, sizeof fb);
    frame = (fhead + fdepth + maxout + 15) / 16 * 16;
    first = V + 8;
    if (f->at.section)
        bprintf(&out, "\t.section %s,\"ax\",@progbits\n", f->at.section);
    else
        bputs(&out, "\t.text\n");
    bputs(&out, "\t.balign 4\n");
    if (f->sclass != SC_STATIC) {
        bprintf(&out, "\t.%s %s\n", f->at.weak ? "weak" : "globl", symlabel(f));
        if (f->at.hidden)
            bprintf(&out, "\t.hidden %s\n", symlabel(f));
    }
    bprintf(&out, "\t.type %s, @function\n", symlabel(f));
    bprintf(&out, "%s:\n", symlabel(f));
    bprintf(&out, "\taddi sp, sp, -%L\n", first);
    bprintf(&out, "\tsw ra, %L(sp)\n", first - 4 - V);
    bprintf(&out, "\tsw s0, %L(sp)\n", first - 8 - V);
    bprintf(&out, "\taddi s0, sp, %L\n", first);
    if (frame - first <= 2048)
        bprintf(&out, "\taddi sp, sp, -%L\n", frame - first);
    else {
        bprintf(&out, "\tli t0, %L\n", frame - first);
        bputs(&out, "\tsub sp, sp, t0\n");
    }
    for (i = 0; i < NPOOL; i++)
        if (poolever[i])
            bprintf(&out, "\tsw %s, %s(s0)\n", poolname[i], i64str(-(V + 12 + 4 * i), num));
    bput(&out, body.p ? body.p : "", body.n);
    for (i = 0; i < NPOOL; i++)
        if (poolever[i])
            bprintf(&out, "\tlw %s, %s(s0)\n", poolname[i], i64str(-(V + 12 + 4 * i), num));
    bprintf(&out, "\tlw ra, %L(s0)\n", -(V + 4));
    bputs(&out, "\tmv sp, s0\n");
    bprintf(&out, "\tlw s0, %L(sp)\n", -(V + 8));
    bputs(&out, "\tret\n");
    bprintf(&out, "\t.size %s, .-%s\n", symlabel(f), symlabel(f));
    free(body.p);
    curfn = 0;
}

/* ==== 静的なデータ ==== */

struct dreloc {
    i64 off;
    const char *label;
    i64 addend;
    struct dreloc *next;
};

static const char *strlabel(struct strlit *l)
{
    char b[32];
    l->used = 1;
    sprintf(b, ".LC%d", l->id);
    return intern(b, (long)strlen(b))->s;
}

/* 番地定数: 記号 + 定数。記号が無ければ *label は 0 */
static int eval_addr(struct node *n, const char **label, i64 *addend);

static int addr_of_lvalue(struct node *n, const char **label, i64 *addend)
{
    switch (n->op) {
    case N_VAR:
        if (!n->sym->global && !n->sym->isstaticlocal)
            return 0;
        mark_ref(n->sym);
        *label = symlabel(n->sym->alias_of ? n->sym->alias_of : n->sym);
        *addend = 0;
        return 1;
    case N_STR:
        *label = strlabel(n->str);
        *addend = 0;
        return 1;
    case N_MEMBER:
        if (!addr_of_lvalue(n->a, label, addend))
            return 0;
        *addend += n->val;
        return 1;
    case N_DEREF:
        return eval_addr(n->a, label, addend);
    }
    return 0;
}

static int eval_addr(struct node *n, const char **label, i64 *addend)
{
    i64 v;
    switch (n->op) {
    case N_NUM:
        *label = 0;
        *addend = n->val;
        return 1;
    case N_ADDR:
        return addr_of_lvalue(n->a, label, addend);
    case N_CAST:
        if (tsize(n->ty) != 4 && !(n->ty->kind == TY_BOOL))
            return 0;
        return eval_addr(n->a, label, addend);
    case N_ADD:
    case N_SUB:
        if (eval_addr(n->a, label, addend) && eval_int(n->b, &v)) {
            *addend = n->op == N_ADD ? *addend + v : *addend - v;
            return 1;
        }
        if (n->op == N_ADD && eval_int(n->a, &v) && eval_addr(n->b, label, addend)) {
            *addend += v;
            return 1;
        }
        return 0;
    case N_VAR:
    case N_STR:
        if (n->ty->kind == TY_ARRAY || n->ty->kind == TY_FUNC)
            return addr_of_lvalue(n, label, addend);
        return 0;
    }
    return 0;
}

static void put_le(unsigned char *img, i64 off, i64 v, int size)
{
    int i;
    for (i = 0; i < size; i++)
        img[off + i] = (unsigned char)(((u64)v >> (8 * i)) & 0xff);
}

static void emit_data(struct sym *s)
{
    i64 size = tsize(s->ty), i;
    unsigned char *img = xmalloc(size + 8);
    struct dreloc *rl = 0, **rt = &rl;
    struct ient *e;
    int zero = 1, isconst = 0;
    const char *lab = symlabel(s);
    int align = talign(s->ty);
    struct type *bt = s->ty;

    if (s->at.aligned > align)
        align = s->at.aligned;
    while (bt->kind == TY_ARRAY)
        bt = bt->base;
    isconst = (s->ty->qual & Q_CONST) || (bt->qual & Q_CONST);
    for (e = s->init; e; e = e->next) {
        i64 v;
        const char *l;
        curfile = e->expr->file;
        curline = e->expr->line;
        if (e->expr->op == N_STR && e->ty->kind == TY_ARRAY) {
            struct strlit *str = e->expr->str;
            i64 n = str->len + 1;
            if (e->ty->len >= 0 && n > e->ty->len)
                n = e->ty->len;
            for (i = 0; i < n; i++)
                img[e->off + i] = i < str->len ? (unsigned char)str->s[i] : 0;
            continue;
        }
        if (e->bf) {
            u64 cur = 0, mask;
            int sz = (int)tsize(e->bf->ty);
            if (!eval_int(e->expr, &v))
                fatal_at(curfile, curline, "ビットフィールドの初期化子が定数でない", 0);
            for (i = 0; i < sz; i++)
                cur |= (u64)img[e->off + i] << (8 * i);
            mask = (e->bf->bw >= 64 ? ~0ULL : ((1ULL << e->bf->bw) - 1)) << e->bf->bo;
            cur = (cur & ~mask) | (((u64)v << e->bf->bo) & mask);
            put_le(img, e->off, (i64)cur, sz);
            continue;
        }
        if (e->ty->kind == TY_STRUCT || e->ty->kind == TY_UNION)
            fatal_at(curfile, curline, "静的な構造体を構造体の値で初期化した", 0);
        if (eval_int(e->expr, &v)) {
            put_le(img, e->off, v, (int)tsize(e->ty));
            continue;
        }
        if (eval_addr(e->expr, &l, &v) && tsize(e->ty) == 4) {
            if (!l) {
                put_le(img, e->off, v, 4);
                continue;
            }
            *rt = xmalloc(sizeof(struct dreloc));
            (*rt)->off = e->off;
            (*rt)->label = l;
            (*rt)->addend = v;
            rt = &(*rt)->next;
            continue;
        }
        fatal_at(curfile, curline, "静的な初期化子が定数でない", s->name->s);
    }
    for (i = 0; i < size; i++)
        if (img[i])
            zero = 0;
    if (rl)
        zero = 0;
    if (s->at.section) {
        int nb = strncmp(s->at.section, ".bss", 4) == 0;
        bprintf(&out, "\t.section %s,\"%s\",@%s\n", s->at.section,
                isconst && !nb ? "a" : "aw", nb ? "nobits" : "progbits");
    } else if (zero)
        bputs(&out, "\t.bss\n");
    else if (isconst)
        bputs(&out, "\t.section .rodata\n");
    else
        bputs(&out, "\t.data\n");
    bprintf(&out, "\t.balign %d\n", align);
    if (s->sclass != SC_STATIC && !s->isstaticlocal) {
        bprintf(&out, "\t.%s %s\n", s->at.weak ? "weak" : "globl", lab);
        if (s->at.hidden)
            bprintf(&out, "\t.hidden %s\n", lab);
    }
    bprintf(&out, "\t.type %s, @object\n", lab);
    bprintf(&out, "%s:\n", lab);
    if (zero && size > 0)
        bprintf(&out, "\t.zero %L\n", size);
    else {
        i64 at = 0;
        struct dreloc *r = rl;
        while (at < size) {
            if (r && r->off == at) {
                if (r->addend)
                    bprintf(&out, "\t.word %s%s%L\n", r->label, r->addend > 0 ? "+" : "", r->addend);
                else
                    bprintf(&out, "\t.word %s\n", r->label);
                at += 4;
                r = r->next;
                continue;
            }
            {
                i64 end = r ? r->off : size, k;
                i64 z = at;
                while (z < end && img[z] == 0)
                    z++;
                if (z - at >= 8) {
                    bprintf(&out, "\t.zero %L\n", z - at);
                    at = z;
                    continue;
                }
                bputs(&out, "\t.byte ");
                for (k = 0; at < end && k < 16; k++, at++) {
                    char b[8];
                    sprintf(b, "%s%d", k ? ", " : "", img[at]);
                    bputs(&out, b);
                }
                bputs(&out, "\n");
            }
        }
    }
    if (size == 0)
        bputs(&out, "\t.zero 0\n");
    bprintf(&out, "\t.size %s, %L\n", lab, size);
    free(img);
}

static void emit_strings(void)
{
    struct strlit *l;
    int any = 0;
    for (l = strlits; l; l = l->next) {
        long i;
        if (!l->used)
            continue;
        if (!any)
            bputs(&out, "\t.section .rodata\n");
        any = 1;
        bprintf(&out, ".LC%d:\n", l->id);
        if (l->wide) {
            for (i = 0; i <= l->len; i++)
                bprintf(&out, "\t.word %d\n", i < l->len ? (unsigned char)l->s[i] : 0);
            continue;
        }
        bputs(&out, "\t.string \"");
        for (i = 0; i < l->len; i++) {
            unsigned char c = (unsigned char)l->s[i];
            char b[8];
            if (c == '"' || c == '\\') {
                b[0] = '\\';
                b[1] = (char)c;
                b[2] = 0;
            } else if (c >= 32 && c < 127) {
                b[0] = (char)c;
                b[1] = 0;
            } else
                sprintf(b, "\\%03o", c);
            bputs(&out, b);
        }
        bputs(&out, "\"\n");
    }
}

/* ==== 単位の出力: 参照されたものだけを出す (docs/stage018-ncc.md 4.3) ==== */

static void emit_unit(void)
{
    struct sym *s;
    struct toplevel_asm *a;
    int progress;
    for (a = tlasm; a; a = a->next) {
        bputs(&out, a->text);
        bputs(&out, "\n");
    }
    /* 根: 外に見える定義と used の付いたもの */
    for (s = globals; s; s = s->gnext) {
        if (s->kind == SY_FUNC && s->defined && s->body) {
            if ((s->sclass != SC_STATIC && !(s->isinline && s->at.gnu_inline)) || s->at.used)
                mark_ref(s);
        } else if (s->kind == SY_VAR && (s->hasinit || s->tentative) && !s->isstaticlocal) {
            if (s->sclass != SC_STATIC || s->at.used)
                mark_ref(s);
        }
        if (s->at.alias)
            mark_ref(s);
    }
    do {
        progress = 0;
        for (s = globals; s; s = s->gnext) {
            if (!s->referenced || s->emitted)
                continue;
            if (s->at.alias) {
                struct name *tn = intern(s->at.alias, (long)strlen(s->at.alias));
                s->emitted = 1;
                progress = 1;
                if (tn->gsym)
                    mark_ref(tn->gsym);
                if (s->sclass != SC_STATIC)
                    bprintf(&out, "\t.%s %s\n", s->at.weak ? "weak" : "globl", symlabel(s));
                bprintf(&out, "\t.set %s, %s\n", symlabel(s), s->at.alias);
                continue;
            }
            if (s->kind == SY_FUNC) {
                if (!s->body) {
                    s->emitted = 1;
                    if (s->sclass == SC_STATIC && !s->isinline)
                        fatal_at(s->file, s->line, "定義の無い static 関数を参照した", s->name->s);
                    if (s->at.weak)
                        bprintf(&out, "\t.weak %s\n", symlabel(s));
                    continue;
                }
                if (s->isinline && s->at.gnu_inline && s->sclass != SC_STATIC) {
                    /* extern inline (gnu89) は外部の定義を出さない */
                    s->emitted = 1;
                    continue;
                }
                s->emitted = 1;
                progress = 1;
                gen_function(s);
                continue;
            }
            if (s->kind == SY_VAR) {
                s->emitted = 1;
                if (!s->hasinit && !s->tentative) {
                    if (s->at.weak)
                        bprintf(&out, "\t.weak %s\n", symlabel(s));
                    continue;
                }
                progress = 1;
                if (s->ty->kind == TY_ARRAY && s->ty->len < 0) {
                    /* 長さの無い仮定義は 1 要素 (GCC と同じ) */
                    s->ty = arrayof(s->ty->base, 1);
                }
                emit_data(s);
            }
        }
    } while (progress);
    emit_strings();
}

/* ==== 入口 ==== */

static char *readall(FILE *f)
{
    long cap = 1 << 20, n = 0;
    char *b = xmalloc(cap);
    int c;
    while ((c = getc(f)) != EOF) {
        if (c == 4)
            break;
        if (n + 2 > cap) {
            cap *= 2;
            b = xrealloc(b, cap);
        }
        b[n++] = (char)c;
    }
    b[n] = 0;
    return b;
}

int main(int argc, char **argv)
{
    const char *outname = 0, *inname = 0;
    FILE *in;
    char *text;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outname = argv[++i];
        else if (strcmp(argv[i], "-mcmodel=medany") == 0)
            medany = 1;
        else if (strcmp(argv[i], "-mcmodel=medlow") == 0)
            medany = 0;
        else if (argv[i][0] == '-' && argv[i][1])
            ;   /* 他の選択肢は受けて無視する */
        else
            inname = argv[i];
    }
    if (inname) {
        in = fopen(inname, "r");
        if (!in) {
            fprintf(stderr, "ncc1: 開けない: %s\n", inname);
            return 1;
        }
        curfile = inname;
    } else
        in = stdin;
    text = readall(in);
    if (in != stdin)
        fclose(in);
    init_types();
    init_keywords();
    push_scope();
    level = 0;
    lex(text);
    tp = 0;
    curfile = toks[0].file;
    curline = toks[0].line;
    translation_unit();
    emit_unit();
    {
        FILE *f = stdout;
        if (outname) {
            f = fopen(outname, "w");
            if (!f) {
                fprintf(stderr, "ncc1: 書けない: %s\n", outname);
                return 1;
            }
        }
        if (out.n)
            fwrite(out.p, 1, out.n, f);
        if (f != stdout)
            fclose(f);
        else
            fflush(stdout);
    }
    return 0;
}
