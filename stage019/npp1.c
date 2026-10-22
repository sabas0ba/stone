/* npp1.c --- C の前処理器 (docs/stage019-linux.md 8 章)
 *
 *   npp1 [選択肢] <入力> [-o <出力>]
 *
 *   -I <dir> / -isystem <dir> / -iquote <dir>   探索の階層
 *   -include <file>                             入力の前に読む
 *   -D <name>[=<値>] / -U <name>                マクロの定義と取消し
 *   -nostdinc / -undef                          受けて何もしない (既定の
 *                                               階層もマクロも持たない)
 *   -P                                          行の印 (# 行 "名前") を出さない
 *   -x c | -x assembler-with-cpp                入力の言語 (既定は拡張子 .S で asm)
 *   -MD <file> / -MMD <file> / -MF <file> / -MT <target>
 *                                               依存の記録 (fixdep が読む形)。
 *                                               -MMD は -isystem の階層の
 *                                               header を記録しない
 *
 * Linux 5.4 の Kbuild がドライバ (ncc) を通して与える形を持つ。処理系が
 * 前もって定義するマクロは持たず，ドライバが -include で与える
 * (stage019/predef-rv32-gcc46.h)。
 *
 * 字句は前処理字句 (C99 6.4) に分け，展開は字句の列の上で行う。再帰の
 * 抑止は字句ごとの隠し集合 (hide set) で行う (D. Prosser の算法。C99 6.10.3.4
 * の規則と同じ結果になる)。GNU の拡張は Linux が使うもの: 名前の付いた
 * 可変長の仮引数 (args...)，`, ## __VA_ARGS__` の読点の削除，__COUNTER__，
 * _Pragma，#warning，#include_next。
 *
 * 同じ header を何度も読まないため，全体が `#ifndef X` / `#define X` …
 * `#endif` で囲まれたファイルは，X が定義されていれば 2 度目から開かない
 * (GCC の multiple-include optimization と同じ判定)。
 *
 * C89 で書く。cc15as (stone) とホストの gcc の両方で組む。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long i64;
typedef unsigned long long u64;

static void die(const char *file, int line, const char *msg, const char *arg)
{
    if (file)
        fprintf(stderr, "npp1: %s:%d: %s%s%s\n", file, line, msg, arg ? ": " : "", arg ? arg : "");
    else
        fprintf(stderr, "npp1: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p)
        die(0, 0, "記憶域が足りない", 0);
    memset(p, 0, n > 0 ? n : 1);
    return p;
}

static void *xrealloc(void *p, long n)
{
    p = realloc(p, n > 0 ? n : 1);
    if (!p)
        die(0, 0, "記憶域が足りない", 0);
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

/* ---- 名前の表 (識別子と字句の綴りを 1 つにまとめる) ---- */

struct name {
    char *s;
    int len;
    struct name *next;
    struct macro *macro;        /* 定義されたマクロ (無ければ 0) */
    int special;                /* 1: __LINE__  2: __FILE__  3: __COUNTER__  4: _Pragma
                                 * 5: __VA_ARGS__  6: defined */
};

#define NHASH 16384
static struct name *names[NHASH];

static struct name *intern(const char *s, int len)
{
    unsigned long h = 2166136261UL;
    int i;
    struct name *n;
    for (i = 0; i < len; i++)
        h = ((h ^ (unsigned char)s[i]) * 16777619UL) & 0xffffffffUL;
    h %= NHASH;
    for (n = names[h]; n; n = n->next)
        if (n->len == len && memcmp(n->s, s, len) == 0)
            return n;
    n = xmalloc(sizeof(struct name));
    n->s = xstrndup(s, len);
    n->len = len;
    n->next = names[h];
    names[h] = n;
    return n;
}

/* ---- 字句 ---- */

#define T_EOF 0
#define T_NL 1
#define T_IDENT 2
#define T_NUM 3
#define T_STR 4
#define T_CHR 5
#define T_PUNCT 6
#define T_OTHER 7
#define T_PLACE 8               /* ## の空の被演算子 (出力しない) */

struct macro;

struct hset {
    struct macro *m;
    struct hset *next;
};

struct tok {
    int kind;
    struct name *n;             /* 綴り */
    int space;                  /* 前に空白があった */
    int noexp;                  /* 二度と展開しない (C99 6.10.3.4) */
    int line;
    struct hset *hs;
    int bnd;                    /* 展開の境: 1 は前が境，2 は後ろが境 (GCC の padding) */
};

struct tlist {
    struct tok *v;
    int n;
    int cap;
};

static void tl_add(struct tlist *l, struct tok *t)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, (long)l->cap * (long)sizeof(struct tok));
    }
    l->v[l->n++] = *t;
}

static int is_punct(struct tok *t, const char *p)
{
    return t->kind == T_PUNCT && strcmp(t->n->s, p) == 0;
}

/* ---- マクロ ---- */

struct macro {
    struct name *name;
    int fn;                     /* 関数形式 */
    int nparam;
    struct name **params;
    int variadic;               /* 最後の仮引数が可変長 */
    struct tlist body;
};

static struct hset *hs_add(struct hset *h, struct macro *m)
{
    struct hset *x = xmalloc(sizeof(struct hset));
    x->m = m;
    x->next = h;
    return x;
}

static int hs_has(struct hset *h, struct macro *m)
{
    for (; h; h = h->next)
        if (h->m == m)
            return 1;
    return 0;
}

static struct hset *hs_union(struct hset *a, struct hset *b)
{
    for (; b; b = b->next)
        if (!hs_has(a, b->m))
            a = hs_add(a, b->m);
    return a;
}

static struct hset *hs_inter(struct hset *a, struct hset *b)
{
    struct hset *r = 0;
    for (; a; a = a->next)
        if (hs_has(b, a->m))
            r = hs_add(r, a->m);
    return r;
}

/* ---- 入力のファイル ---- */

struct cond {
    int active;                 /* この枝を出力している */
    int taken;                  /* どれかの枝を選んだ */
    int seen_else;
    int guard;                  /* ファイルの頭の #ifndef (include guard の候補) */
};

struct src {
    const char *path;
    const char *dir;            /* "..." の探索の起点 */
    int diridx;                 /* 見つけた探索の階層の番号 (#include_next のため。-1 は起点) */
    char *buf;
    long pos;
    long len;
    int line;
    int bol;                    /* 行の頭 (空白だけを読んだ) */
    int ncond0;                 /* 入った時の条件の深さ */
    struct src *up;
    /* include guard の判定 */
    int gstate;                 /* 0: 頭の #ifndef を待つ 1: #define を待つ 2: 中 3: 閉じた後 -1: 候補でない */
    struct name *gname;
    int gdepth;
    int dep_pending;            /* 読み始めたら依存に記録する */
};

static struct src *cur;
static struct cond *conds;
static int ncond, ccond;

struct guard {
    const char *path;
    struct name *macro;
    struct guard *next;
};

static struct guard *guards;

static int asm_mode;
static int no_markers;
static int counter;

/* 探索の階層。種類 0: -iquote 1: -I 2: -isystem。順は与えた順 */
struct idir {
    const char *path;
    int kind;
};

static struct idir *idirs;
static int nidir;

/* 依存の記録 */
static int dep_kind;            /* 0: なし 1: -MD 2: -MMD */
static const char *dep_file, *dep_target;
static char **deps;
static int ndeps;

/* 依存に記録する。GCC (libcpp) に合わせて
 *   - 頭の ./ を除く (mkdeps.cc の apply_vpath)
 *   - 同じ項は 1 度だけ記録する。GCC はファイルを (書かれた名前, 見つけた
 *     探索の階層) の項として覚え，項ごとに初めて読んだ時に記録する。同じ経路
 *     でも <linux/x.h> を include/uapi で見つけたものと <uapi/linux/x.h> を
 *     include で見つけたものは別の項なので 2 度記録する。dir は見つけた階層の
 *     番号 (-1 は取り込んだ側の場所，-2 は命令行の入力と -include)。経路は
 *     階層と名前をつないだものなので，(dir, 経路) が項を決める */
static int *dep_dir;

static void add_dep(const char *path, int dir)
{
    int i;
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
        while (*path == '/')
            path++;
    }
    for (i = 0; i < ndeps; i++)
        if (dep_dir[i] == dir && strcmp(deps[i], path) == 0)
            return;
    deps = xrealloc(deps, (long)(ndeps + 1) * (long)sizeof(char *));
    dep_dir = xrealloc(dep_dir, (long)(ndeps + 1) * (long)sizeof(int));
    dep_dir[ndeps] = dir;
    deps[ndeps++] = xstrdup(path);
}

static char *readfile(const char *path, long *len)
{
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    long cap = 65536, n = 0, r;
    char *b;
    if (!f)
        return 0;
    b = xmalloc(cap + 2);
    while ((r = (long)fread(b + n, 1, (size_t)(cap - n), f)) > 0) {
        n += r;
        if (n == cap) {
            cap *= 2;
            b = xrealloc(b, cap + 2);
        }
    }
    if (!is_stdin)
        fclose(f);
    b[n] = '\n';                /* 最後の行が改行で終わらなくてもよいように */
    b[n + 1] = 0;
    *len = n + 1;
    return b;
}

static char *dirname_of(const char *path)
{
    const char *s = strrchr(path, '/');
    if (!s)
        return xstrdup("");
    return xstrndup(path, (long)(s - path) + 1);
}

/* ---- 出力 ---- */

static FILE *out;
static int out_bol = 1;         /* 出力の行の頭 */
static struct tok last_out;     /* 直前に出した字句 (空白の判断のため) */
static int have_last;
static const char *mark_path;   /* 最後に印を出したファイル */
static int mark_line;
static int out_line;            /* 出力の行が入力のどの行に当たるか (印のため) */

static void put_marker(struct src *s, int flag)
{
    if (no_markers)
        return;
    if (!out_bol)
        putc('\n', out);
    fprintf(out, "# %d \"%s\"", s->line, s->path);
    if (flag)
        fprintf(out, " %d", flag);
    putc('\n', out);
    out_bol = 1;
    have_last = 0;
    mark_path = s->path;
    mark_line = s->line;
    out_line = s->line;
}

/* 2 つの字句を空白無しで並べると 1 つに読めてしまうか (GCC の avoid_paste に当たる) */
static int would_paste(struct tok *a, struct tok *b)
{
    int ca, cb;
    if (a->kind == T_PLACE || b->kind == T_PLACE)
        return 0;
    ca = a->n->s[a->n->len - 1];
    cb = b->n->s[0];
    if ((a->kind == T_IDENT || a->kind == T_NUM)
        && (b->kind == T_IDENT || b->kind == T_NUM || b->kind == T_STR || b->kind == T_CHR))
        return 1;
    if (a->kind == T_NUM && (cb == '.' || cb == '+' || cb == '-'))
        return 1;
    if (a->kind != T_PUNCT || b->kind == T_IDENT || b->kind == T_STR || b->kind == T_CHR)
        return 0;
    /* GCC の cpp_avoid_paste の表。= が後ろに付いて 1 つになる演算子 */
    if (cb == '=' && a->n->len <= 2 && strchr("=!><+-*/%&|^", a->n->s[0])
        && (a->n->len == 1 || (a->n->s[1] == a->n->s[0] && strchr("<>", ca))))
        return 1;
    if (a->n->len != 1 && !is_punct(a, "->"))
        return 0;
    if (is_punct(a, "->"))
        return cb == '*';
    switch (ca) {
    case '>': return cb == '>';
    case '<': return cb == '<' || cb == '%' || cb == ':';
    case '+': return cb == '+';
    case '-': return cb == '-' || cb == '>';
    case '/': return cb == '/' || cb == '*';
    case '%': return cb == ':' || cb == '%';
    case '&': return cb == '&';
    case '|': return cb == '|';
    case ':': return cb == ':' || cb == '>';
    case '.': return cb == '.' || cb == '%' || b->kind == T_NUM;
    case '#': return cb == '#' || cb == '%';
    default: return 0;
    }
}

static void emit(struct tok *t)
{
    if (t->kind == T_PLACE)
        return;
    if (t->kind == T_NL) {
        /* -P では空行を出さない (GCC 13 と同じ。gcc-version.sh などが出力の
         * 最後の行を読む) */
        if (no_markers && out_bol) {
            out_line++;
            return;
        }
        putc('\n', out);
        out_bol = 1;
        have_last = 0;
        out_line++;
        return;
    }
    /* GCC と同じく，貼り付きの回避は展開の境 (マクロの置換の前後と実引数の
     * 前後) でだけ行う。ファイルやマクロの本体で隣り合っていた字句はそのまま並べる */
    if (!out_bol && have_last
        && (t->space || (((t->bnd & 1) || (last_out.bnd & 2)) && would_paste(&last_out, t))))
        putc(' ', out);
    else if (out_bol && t->space && asm_mode)
        putc(' ', out);
    fputs(t->n->s, out);
    out_bol = 0;
    last_out = *t;
    have_last = 1;
}

/* ---- 字句の読取り ---- */

static const char *puncts[] = {
    "%:%:", "...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
    "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##", "<:", ":>", "<%",
    "%>", "%:", 0
};

static int peekc(struct src *s, long k)
{
    long p = s->pos;
    /* 行の継続 (\ 改行) を飛ばして k 文字先を見る */
    for (;;) {
        while (s->buf[p] == '\\' && s->buf[p + 1] == '\n')
            p += 2;
        if (p >= s->len)
            return -1;
        if (k == 0)
            return (unsigned char)s->buf[p];
        p++;
        k--;
    }
}

static int getch(struct src *s)
{
    int c;
    while (s->buf[s->pos] == '\\' && s->buf[s->pos + 1] == '\n') {
        s->pos += 2;
        s->line++;
    }
    if (s->pos >= s->len)
        return -1;
    c = (unsigned char)s->buf[s->pos++];
    if (c == '\n')
        s->line++;
    return c;
}

static int isidc(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
        || c == '$';
}

/* 字句の綴りを溜める器 */
static char *lb;
static long lbn, lbcap;

static void lb_put(int c)
{
    if (lbn + 2 > lbcap) {
        lbcap = lbcap ? lbcap * 2 : 1024;
        lb = xrealloc(lb, lbcap);
    }
    lb[lbn++] = (char)c;
}

/* 空白と注釈を飛ばす。改行は飛ばさない。空白があれば 1 */
static int skip_space(struct src *s)
{
    int sp = 0;
    for (;;) {
        int c = peekc(s, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            getch(s);
            sp = 1;
            continue;
        }
        if (c == '/' && peekc(s, 1) == '*') {
            int line = s->line;
            getch(s);
            getch(s);
            for (;;) {
                c = getch(s);
                if (c < 0)
                    die(s->path, line, "注釈が閉じていない", 0);
                if (c == '*' && peekc(s, 0) == '/') {
                    getch(s);
                    break;
                }
            }
            sp = 1;
            continue;
        }
        if (c == '/' && peekc(s, 1) == '/') {
            while (peekc(s, 0) != '\n' && peekc(s, 0) >= 0)
                getch(s);
            sp = 1;
            continue;
        }
        return sp;
    }
}

/* 1 つの字句を読む。#include の <...> は hdr を 1 にして読む */
static void lex(struct src *s, struct tok *t, int hdr)
{
    int c, q;
    memset(t, 0, sizeof *t);
    t->space = skip_space(s);
    t->line = s->line;
    lbn = 0;
    c = peekc(s, 0);
    if (c < 0) {
        t->kind = T_EOF;
        t->n = intern("", 0);
        return;
    }
    if (c == '\n') {
        getch(s);
        t->kind = T_NL;
        t->n = intern("\n", 1);
        return;
    }
    if (hdr && c == '<') {
        while ((c = getch(s)) >= 0 && c != '>' && c != '\n')
            lb_put(c);
        if (c != '>')
            die(s->path, t->line, "#include の <...> が閉じていない", 0);
        lb_put('>');
        t->kind = T_STR;
        t->n = intern(lb, (int)lbn);
        return;
    }
    /* 文字列と文字の定数 (前置の L・u・U・u8 を含む) */
    q = 0;
    if (c == '"' || c == '\'')
        q = c;
    else if ((c == 'L' || c == 'U' || c == 'u') && (peekc(s, 1) == '"' || peekc(s, 1) == '\''))
        q = peekc(s, 1);
    else if (c == 'u' && peekc(s, 1) == '8' && peekc(s, 2) == '"')
        q = '"';
    if (q) {
        long save = s->pos;
        int sline = s->line, ok = 0;
        while ((c = peekc(s, 0)) != q)
            lb_put(getch(s));
        lb_put(getch(s));
        for (;;) {
            c = peekc(s, 0);
            if (c < 0 || c == '\n')
                break;
            lb_put(getch(s));
            if (c == '\\') {
                c = peekc(s, 0);
                if (c >= 0 && c != '\n')
                    lb_put(getch(s));
                continue;
            }
            if (c == q) {
                ok = 1;
                break;
            }
        }
        if (ok) {
            t->kind = q == '"' ? T_STR : T_CHR;
            t->n = intern(lb, (int)lbn);
            return;
        }
        /* 閉じない引用符: asm では普通の文字として読む (注釈の中の ' など) */
        if (!asm_mode)
            die(s->path, sline, "文字列か文字の定数が閉じていない", 0);
        s->pos = save;
        s->line = sline;
        lbn = 0;
        c = getch(s);
        lb_put(c);
        t->kind = T_OTHER;
        t->n = intern(lb, (int)lbn);
        return;
    }
    if (isidc(c) && !(c >= '0' && c <= '9')) {
        while (isidc(peekc(s, 0)))
            lb_put(getch(s));
        t->kind = T_IDENT;
        t->n = intern(lb, (int)lbn);
        return;
    }
    if ((c >= '0' && c <= '9') || (c == '.' && peekc(s, 1) >= '0' && peekc(s, 1) <= '9')) {
        lb_put(getch(s));
        for (;;) {
            c = peekc(s, 0);
            if ((c == '+' || c == '-') && lbn > 0
                && (lb[lbn - 1] == 'e' || lb[lbn - 1] == 'E' || lb[lbn - 1] == 'p'
                    || lb[lbn - 1] == 'P')) {
                lb_put(getch(s));
                continue;
            }
            if (isidc(c) || c == '.') {
                lb_put(getch(s));
                continue;
            }
            break;
        }
        t->kind = T_NUM;
        t->n = intern(lb, (int)lbn);
        return;
    }
    {
        int i;
        for (i = 0; puncts[i]; i++) {
            int k, l = (int)strlen(puncts[i]);
            for (k = 0; k < l; k++)
                if (peekc(s, k) != (unsigned char)puncts[i][k])
                    break;
            if (k == l) {
                for (k = 0; k < l; k++)
                    lb_put(getch(s));
                t->kind = T_PUNCT;
                t->n = intern(lb, (int)lbn);
                return;
            }
        }
    }
    lb_put(getch(s));
    t->kind = strchr("[](){}.&*+-~!/%<>^|?:;=,#", c) ? T_PUNCT : T_OTHER;
    t->n = intern(lb, (int)lbn);
}

/* ---- 字句の供給 ----
 * 展開の結果を押し戻す積み (pend) が先で，尽きればファイルを読む。
 * 前処理の指令はファイルから行の頭の # を読んだ時に処理する */

struct pend {
    struct tok *v;
    int n;
    int i;
    struct pend *up;
};

struct ctx {
    struct pend *p;
    int file;                   /* 尽きたらファイルを読む */
    int pend_bnd;               /* 空に展開した直後。次の字句の前が展開の境 */
};

static void directive(struct src *s);
static int lex_file(struct tok *t);

static void push_list(struct ctx *c, struct tok *v, int n)
{
    struct pend *p;
    if (n <= 0)
        return;
    p = xmalloc(sizeof(struct pend));
    p->v = xmalloc((long)n * (long)sizeof(struct tok));
    memcpy(p->v, v, (size_t)n * sizeof(struct tok));
    p->n = n;
    p->up = c->p;
    c->p = p;
}

static int next_tok(struct ctx *c, struct tok *t)
{
    while (c->p) {
        struct pend *p = c->p;
        if (p->i < p->n) {
            *t = p->v[p->i++];
            return 1;
        }
        c->p = p->up;
        free(p->v);
        free(p);
    }
    if (c->file)
        return lex_file(t);
    return 0;
}

static void push_file(const char *path, char *buf, long len, int diridx);

static int skipping(void)
{
    return ncond > 0 && !conds[ncond - 1].active;
}

/* include guard の判定: 有効な字句か指令がファイルの頭の #ifndef の外に現れた */
static void guard_touch(struct src *s)
{
    if (s->gstate == 0 || s->gstate == 1 || s->gstate == 3)
        s->gstate = -1;
}

static void pop_file(void)
{
    struct src *s = cur;
    if (ncond != s->ncond0)
        die(s->path, s->line, "#if が閉じていない", 0);
    if (s->gstate == 3 && s->gname) {
        struct guard *g = xmalloc(sizeof(struct guard));
        g->path = s->path;
        g->macro = s->gname;
        g->next = guards;
        guards = g;
    }
    cur = s->up;
    free(s->buf);
    free(s);
    if (cur)
        put_marker(cur, 2);
}

/* ファイルから字句を 1 つ読む。指令は処理し，読み飛ばす枝の字句は捨てる */
static int lex_file(struct tok *t)
{
    for (;;) {
        struct src *s = cur;
        if (!s)
            return 0;
        if (s->dep_pending) {
            s->dep_pending = 0;
            add_dep(s->path, -2);
        }
        lex(s, t, 0);
        if (t->kind == T_EOF) {
            pop_file();
            continue;
        }
        if (t->kind == T_NL) {
            s->bol = 1;
            if (skipping())
                continue;
            return 1;
        }
        if (s->bol && is_punct(t, "#")) {
            s->bol = 0;
            directive(s);
            s->bol = 1;
            continue;
        }
        s->bol = 0;
        if (skipping())
            continue;
        guard_touch(s);
        return 1;
    }
}

/* ---- 展開 ---- */

static struct macro *macro_of(struct tok *t)
{
    if (t->kind != T_IDENT || t->noexp)
        return 0;
    return t->n->macro;
}

static void expand_list(struct tok *in, int n, struct tlist *outl);

static void stringify(struct tok *v, int n, struct tok *res)
{
    int i, k;
    lbn = 0;
    lb_put('"');
    for (i = 0; i < n; i++) {
        const char *p;
        if (v[i].kind == T_PLACE)
            continue;
        if (i > 0 && v[i].space && lbn > 1)
            lb_put(' ');
        p = v[i].n->s;
        for (k = 0; p[k]; k++) {
            if ((v[i].kind == T_STR || v[i].kind == T_CHR) && (p[k] == '"' || p[k] == '\\'))
                lb_put('\\');
            lb_put(p[k]);
        }
    }
    lb_put('"');
    memset(res, 0, sizeof *res);
    res->kind = T_STR;
    res->n = intern(lb, (int)lbn);
}

static int paste_failed;

/* 2 つの字句を ## でつなぐ。1 つの字句にならなければ C では誤り。
 * assembler-with-cpp では GCC と同じく 2 つの字句のまま並べる (paste_failed) */
static struct tok paste(struct tok *a, struct tok *b, const char *file, int line)
{
    struct tok r;
    struct src tmp;
    char *buf;
    long la, lbl;
    paste_failed = 0;
    if (a->kind == T_PLACE)
        return *b;
    if (b->kind == T_PLACE)
        return *a;
    la = a->n->len;
    lbl = b->n->len;
    buf = xmalloc(la + lbl + 3);
    memcpy(buf, a->n->s, la);
    memcpy(buf + la, b->n->s, lbl);
    buf[la + lbl] = '\n';
    buf[la + lbl + 1] = 0;
    memset(&tmp, 0, sizeof tmp);
    tmp.buf = buf;
    tmp.len = la + lbl + 1;
    tmp.path = file;
    lex(&tmp, &r, 0);
    if (tmp.pos != la + lbl) {
        if (!asm_mode)
            die(file, line, "## が 1 つの字句を作らない", buf);
        free(buf);
        paste_failed = 1;
        return *a;
    }
    free(buf);
    r.space = a->space;
    r.hs = 0;
    r.noexp = 0;
    return r;
}

/* os の末尾の字句に b をつなぐ */
static void paste_into(struct tlist *os, struct tok *b, const char *file, int line)
{
    os->v[os->n - 1] = paste(&os->v[os->n - 1], b, file, line);
    if (paste_failed) {
        /* 右の字句は前の空白の有無を保つ (GCC と同じ) */
        tl_add(os, b);
    }
}

struct arg {
    struct tok *v;              /* 生の実引数 */
    int n;
    struct tlist exp;           /* 展開した実引数 */
    int expanded;
};

static int param_index(struct macro *m, struct name *n)
{
    int i;
    for (i = 0; i < m->nparam; i++)
        if (m->params[i] == n)
            return i;
    return -1;
}

/* 置換の列を作る (C99 6.10.3.1〜6.10.3.3) */
static void subst(struct macro *m, struct arg *args, struct hset *hs, struct tok *callt,
                  struct tlist *res, const char *file, int line)
{
    struct tok *body = m->body.v;
    int n = m->body.n, i, k;
    struct tlist os;
    int pend_bnd = 0;
    memset(&os, 0, sizeof os);
    for (i = 0; i < n; i++) {
        struct tok *t = &body[i];
        int pi = m->fn && t->kind == T_IDENT ? param_index(m, t->n) : -1;
        int next_paste = i + 1 < n && is_punct(&body[i + 1], "##");
        if (m->fn && is_punct(t, "#") && i + 1 < n && body[i + 1].kind == T_IDENT
            && param_index(m, body[i + 1].n) >= 0) {
            struct tok st;
            int pj = param_index(m, body[i + 1].n);
            stringify(args[pj].v, args[pj].n, &st);
            st.space = t->space;
            tl_add(&os, &st);
            i++;
            continue;
        }
        if (is_punct(t, "##") && os.n > 0 && i + 1 < n) {
            struct tok *rhs = &body[i + 1];
            int pj = m->fn && rhs->kind == T_IDENT ? param_index(m, rhs->n) : -1;
            struct tok lhs = os.v[os.n - 1];
            if (pj >= 0) {
                struct arg *a = &args[pj];
                /* GNU: , ## __VA_ARGS__ は可変長の実引数が空なら読点を消す */
                if (m->variadic && pj == m->nparam - 1 && is_punct(&lhs, ",")) {
                    if (a->n == 0)
                        os.n--;
                    else
                        for (k = 0; k < a->n; k++)
                            tl_add(&os, &a->v[k]);
                    i++;
                    continue;
                }
                if (a->n == 0) {
                    i++;
                    continue;
                }
                paste_into(&os, &a->v[0], file, line);
                for (k = 1; k < a->n; k++)
                    tl_add(&os, &a->v[k]);
            } else if (m->fn && is_punct(rhs, "#") && i + 2 < n && body[i + 2].kind == T_IDENT
                       && param_index(m, body[i + 2].n) >= 0) {
                struct tok st;
                int pk = param_index(m, body[i + 2].n);
                stringify(args[pk].v, args[pk].n, &st);
                paste_into(&os, &st, file, line);
                i++;
            } else
                paste_into(&os, rhs, file, line);
            i++;
            continue;
        }
        if (pi >= 0) {
            struct arg *a = &args[pi];
            int prev_paste = i > 0 && is_punct(&body[i - 1], "##");
            if (next_paste || prev_paste) {
                if (a->n == 0) {
                    struct tok pl;
                    memset(&pl, 0, sizeof pl);
                    pl.kind = T_PLACE;
                    pl.n = intern("", 0);
                    tl_add(&os, &pl);
                } else
                    for (k = 0; k < a->n; k++) {
                        struct tok x = a->v[k];
                        if (k == 0)
                            x.space = t->space;
                        tl_add(&os, &x);
                    }
                continue;
            }
            if (!a->expanded) {
                expand_list(a->v, a->n, &a->exp);
                a->expanded = 1;
            }
            for (k = 0; k < a->exp.n; k++) {
                struct tok x = a->exp.v[k];
                if (k == 0) {
                    x.space = t->space;
                    x.bnd |= 1;
                }
                if (k == a->exp.n - 1)
                    x.bnd |= 2;
                tl_add(&os, &x);
            }
            /* 空の実引数の後ろも境 */
            pend_bnd = a->exp.n == 0;
            continue;
        }
        tl_add(&os, t);
        if (pend_bnd) {
            os.v[os.n - 1].bnd |= 1;
            pend_bnd = 0;
        }
    }
    for (i = 0; i < os.n; i++) {
        if (os.v[i].kind == T_PLACE)
            continue;
        os.v[i].hs = hs_union(os.v[i].hs, hs);
        os.v[i].line = callt->line;
        tl_add(res, &os.v[i]);
    }
    if (res->n > 0) {
        res->v[0].space = callt->space;
        res->v[0].bnd |= 1;
        res->v[res->n - 1].bnd |= 2;
    }
    free(os.v);
}

static const char *cur_file(void)
{
    return cur ? cur->path : "";
}

static int cur_line(void)
{
    return cur ? cur->line : 0;
}

/* 関数形式のマクロの実引数を読む。( は読んだ後。) を *rp に置く */
static struct arg *collect_args(struct ctx *c, struct macro *m, struct tok *rp)
{
    int depth = 0, na = 0, cap = m->nparam > 0 ? m->nparam : 1, space = 0;
    struct arg *args = xmalloc((long)cap * (long)sizeof(struct arg));
    struct tlist cura;
    struct tok t;
    memset(&cura, 0, sizeof cura);
    for (;;) {
        if (!next_tok(c, &t))
            die(cur_file(), cur_line(), "マクロの実引数が閉じていない", m->name->s);
        if (t.kind == T_NL) {
            space = 1;
            continue;
        }
        if (space) {
            t.space = 1;
            space = 0;
        }
        if (depth == 0 && (is_punct(&t, ")")
                           || (is_punct(&t, ",") && !(m->variadic && na == m->nparam - 1)))) {
            if (na >= cap) {
                cap *= 2;
                args = xrealloc(args, (long)cap * (long)sizeof(struct arg));
            }
            memset(&args[na], 0, sizeof(struct arg));
            args[na].v = cura.v;
            /* 実引数の頭の空白の有無は呼び出しのとおりに保つ。置き換える所では
             * 仮引数の側の空白に替え，, ## __VA_ARGS__ ではそのまま出す (GCC と同じ) */
            args[na].n = cura.n;
            na++;
            memset(&cura, 0, sizeof cura);
            if (is_punct(&t, ")")) {
                *rp = t;
                break;
            }
            continue;
        }
        if (is_punct(&t, "("))
            depth++;
        else if (is_punct(&t, ")"))
            depth--;
        tl_add(&cura, &t);
    }
    /* f() は仮引数 1 つのマクロでは空の実引数 1 つ。仮引数 0 のマクロでは実引数 0 */
    if (m->nparam == 0 && na == 1 && args[0].n == 0)
        na = 0;
    if (m->variadic && na == m->nparam - 1) {
        /* 可変長の部分が全く無い (GNU の拡張として受ける) */
        if (na >= cap)
            args = xrealloc(args, (long)(na + 1) * (long)sizeof(struct arg));
        memset(&args[na], 0, sizeof(struct arg));
        na++;
    }
    if (na != m->nparam)
        die(cur_file(), cur_line(), "マクロの実引数の数が合わない", m->name->s);
    return args;
}

static void do_pragma_op(struct ctx *c, struct tlist *outl, int stream);

/* 特別なマクロ (__LINE__ など) を字句にする。展開したら 1 */
static int special(struct ctx *c, struct tok *t, struct tlist *outl, int stream)
{
    char b[64];
    struct tok r = *t;
    switch (t->n->special) {
    case 1:
        sprintf(b, "%d", cur_line());
        r.kind = T_NUM;
        r.n = intern(b, (int)strlen(b));
        break;
    case 2: {
        const char *f = cur_file();
        long i;
        lbn = 0;
        lb_put('"');
        for (i = 0; f[i]; i++) {
            if (f[i] == '"' || f[i] == '\\')
                lb_put('\\');
            lb_put(f[i]);
        }
        lb_put('"');
        r.kind = T_STR;
        r.n = intern(lb, (int)lbn);
        break;
    }
    case 3:
        sprintf(b, "%d", counter++);
        r.kind = T_NUM;
        r.n = intern(b, (int)strlen(b));
        break;
    case 4:
        do_pragma_op(c, outl, stream);
        return 1;
    default:
        return 0;
    }
    r.noexp = 1;
    if (stream)
        emit(&r);
    else
        tl_add(outl, &r);
    return 1;
}

/* 展開を進める。stream なら出力へ，そうでなければ outl へ */
static void expand(struct ctx *c, struct tlist *outl, int stream)
{
    struct tok t;
    while (next_tok(c, &t)) {
        struct macro *m;
        if (c->pend_bnd && t.kind != T_NL) {
            t.bnd |= 1;
            c->pend_bnd = 0;
        }
        m = macro_of(&t);
        if (t.kind == T_IDENT && !t.noexp && t.n->special && !m) {
            if (special(c, &t, outl, stream))
                continue;
        }
        if (m && hs_has(t.hs, m))
            t.noexp = 1;
        else if (m) {
            struct tlist res;
            memset(&res, 0, sizeof res);
            if (!m->fn) {
                subst(m, 0, hs_add(t.hs, m), &t, &res, cur_file(), cur_line());
                c->pend_bnd = res.n == 0;
                push_list(c, res.v, res.n);
                free(res.v);
                continue;
            } else {
                /* ( が続くか。改行を飛ばして見る (飛ばした改行は戻す) */
                struct tok la[64];
                int nla = 0, found = 0;
                struct tok u;
                while (nla < 64 && next_tok(c, &u)) {
                    if (u.kind == T_NL) {
                        la[nla++] = u;
                        continue;
                    }
                    if (is_punct(&u, "("))
                        found = 1;
                    else
                        la[nla++] = u;
                    break;
                }
                if (found) {
                    struct tok rp;
                    struct arg *args = collect_args(c, m, &rp);
                    struct hset *hs = hs_add(hs_inter(t.hs, rp.hs), m);
                    int i;
                    subst(m, args, hs, &t, &res, cur_file(), cur_line());
                    for (i = 0; i < m->nparam; i++) {
                        free(args[i].v);
                        free(args[i].exp.v);
                    }
                    free(args);
                    /* 実引数の中で読み飛ばした改行は展開の後に出す */
                    push_list(c, la, nla);
                    c->pend_bnd = res.n == 0;
                    push_list(c, res.v, res.n);
                    free(res.v);
                    continue;
                }
                push_list(c, la, nla);
            }
        }
        if (stream)
            emit(&t);
        else
            tl_add(outl, &t);
    }
}

static void expand_list(struct tok *in, int n, struct tlist *outl)
{
    struct ctx c;
    memset(&c, 0, sizeof c);
    push_list(&c, in, n);
    expand(&c, outl, 0);
}

/* _Pragma("...") を #pragma の行にする */
static void do_pragma_op(struct ctx *c, struct tlist *outl, int stream)
{
    struct tok t, s;
    struct tlist raw, ex;
    char *p;
    long i, n;
    int depth = 0;
    if (!next_tok(c, &t) || !is_punct(&t, "("))
        die(cur_file(), cur_line(), "_Pragma の ( が要る", 0);
    /* 被演算子は GCC と同じくマクロを展開してから文字列 1 つになるものを受ける
     * (Linux の __diag は _Pragma(__diag_str(...)) と書く) */
    memset(&raw, 0, sizeof raw);
    memset(&ex, 0, sizeof ex);
    for (;;) {
        if (!next_tok(c, &t))
            die(cur_file(), cur_line(), "_Pragma の ) が要る", 0);
        if (t.kind == T_NL)
            continue;
        if (is_punct(&t, "("))
            depth++;
        else if (is_punct(&t, ")") && depth-- == 0)
            break;
        tl_add(&raw, &t);
    }
    expand_list(raw.v, raw.n, &ex);
    if (ex.n != 1 || ex.v[0].kind != T_STR)
        die(cur_file(), cur_line(), "_Pragma の文字列が要る", 0);
    s = ex.v[0];
    free(raw.v);
    free(ex.v);
    p = s.n->s;
    n = s.n->len;
    if (!stream) {
        /* 実引数の中の _Pragma は字句のまま残し，出力へ再走査する時に行にする */
        struct tok k;
        memset(&k, 0, sizeof k);
        k.kind = T_IDENT;
        k.n = intern("_Pragma", 7);
        k.space = 1;
        tl_add(outl, &k);
        k.kind = T_PUNCT;
        k.n = intern("(", 1);
        k.space = 0;
        tl_add(outl, &k);
        s.space = 0;
        tl_add(outl, &s);
        t.space = 0;
        tl_add(outl, &t);
        return;
    }
    if (!out_bol)
        putc('\n', out);
    fputs("#pragma ", out);
    for (i = 1; i < n - 1; i++) {
        if (p[i] == '\\' && (p[i + 1] == '"' || p[i + 1] == '\\'))
            i++;
        putc(p[i], out);
    }
    putc('\n', out);
    out_bol = 1;
    have_last = 0;
}

/* ---- 指令 ---- */

/* 行の残りの字句を読む (改行は読むが返さない) */
static void read_line(struct src *s, struct tlist *l, int hdr)
{
    struct tok t;
    for (;;) {
        lex(s, &t, hdr && l->n == 0);
        if (t.kind == T_NL || t.kind == T_EOF)
            return;
        tl_add(l, &t);
    }
}

static struct macro *define_macro(struct tok *v, int n, const char *file, int line)
{
    struct macro *m;
    int i = 1;
    if (n < 1 || v[0].kind != T_IDENT)
        die(file, line, "#define の名前が要る", 0);
    m = xmalloc(sizeof(struct macro));
    m->name = v[0].n;
    if (n > 1 && is_punct(&v[1], "(") && !v[1].space) {
        m->fn = 1;
        i = 2;
        if (i < n && is_punct(&v[i], ")"))
            i++;
        else
            for (;;) {
                if (i >= n)
                    die(file, line, "#define の仮引数が閉じていない", m->name->s);
                if (is_punct(&v[i], "...")) {
                    m->params = xrealloc(m->params, (long)(m->nparam + 1) * (long)sizeof(struct name *));
                    m->params[m->nparam++] = intern("__VA_ARGS__", 11);
                    m->variadic = 1;
                    i++;
                } else if (v[i].kind == T_IDENT) {
                    m->params = xrealloc(m->params, (long)(m->nparam + 1) * (long)sizeof(struct name *));
                    m->params[m->nparam++] = v[i].n;
                    i++;
                    if (i < n && is_punct(&v[i], "...")) {
                        m->variadic = 1;        /* GNU: args... */
                        i++;
                    }
                } else
                    die(file, line, "#define の仮引数が名前でない", m->name->s);
                if (i < n && is_punct(&v[i], ")")) {
                    i++;
                    break;
                }
                if (i < n && is_punct(&v[i], ",") && !m->variadic) {
                    i++;
                    continue;
                }
                die(file, line, "#define の仮引数の並びが誤っている", m->name->s);
            }
    }
    for (; i < n; i++) {
        struct tok x = v[i];
        if (m->body.n == 0)
            x.space = 0;
        x.hs = 0;
        tl_add(&m->body, &x);
    }
    v[0].n->macro = m;
    return m;
}

/* -D name[=値] */
static void define_cmdline(const char *d)
{
    const char *eq = strchr(d, '=');
    char *text;
    struct src s;
    struct tlist l;
    if (eq) {
        text = xmalloc((long)strlen(d) + 4);
        memcpy(text, d, (size_t)(eq - d));
        text[eq - d] = ' ';
        strcpy(text + (eq - d) + 1, eq + 1);
    } else {
        text = xmalloc((long)strlen(d) + 4);
        strcpy(text, d);
        strcat(text, " 1");
    }
    strcat(text, "\n");
    memset(&s, 0, sizeof s);
    s.buf = text;
    s.len = (long)strlen(text);
    s.path = "<command-line>";
    memset(&l, 0, sizeof l);
    read_line(&s, &l, 0);
    define_macro(l.v, l.n, "<command-line>", 0);
}

/* #if の式の値 */
struct val {
    i64 v;
    int uns;
};

static struct tok *ev;
static int nev, iev;
static const char *ef;
static int el;

static struct val expr_cond(void);

static struct val mkval(i64 v, int uns)
{
    struct val r;
    r.v = v;
    r.uns = uns;
    return r;
}

static struct val num_value(struct tok *t)
{
    const char *s = t->n->s;
    u64 v = 0;
    int base = 10, uns = 0, i = 0;
    if (t->kind == T_CHR) {
        /* 文字の定数: 先頭の 1 文字 (エスケープを含む) */
        const char *p = strchr(s, '\'') + 1;
        i64 c;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                c = 0;
                while (*p >= '0' && *p <= '7')
                    c = c * 8 + (*p++ - '0');
                break;
            case 'x':
                c = 0;
                p++;
                while (1) {
                    int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
                        : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
                    if (d < 0)
                        break;
                    c = c * 16 + d;
                    p++;
                }
                break;
            default: c = (unsigned char)*p;
            }
        } else
            c = (unsigned char)*p;
        return mkval(c, 0);
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (s[0] == '0')
        base = 8;
    for (; s[i]; i++) {
        int d, ch = s[i];
        if (ch >= '0' && ch <= '9')
            d = ch - '0';
        else if (base == 16 && ch >= 'a' && ch <= 'f')
            d = ch - 'a' + 10;
        else if (base == 16 && ch >= 'A' && ch <= 'F')
            d = ch - 'A' + 10;
        else
            break;
        v = v * (u64)base + (u64)d;
    }
    for (; s[i]; i++) {
        if (s[i] == 'u' || s[i] == 'U')
            uns = 1;
        else if (s[i] != 'l' && s[i] != 'L')
            die(ef, el, "#if の数が誤っている", s);
    }
    if (v > 0x7fffffffffffffffULL)
        uns = 1;
    return mkval((i64)v, uns);
}

static struct val expr_primary(void)
{
    struct tok *t;
    if (iev >= nev)
        die(ef, el, "#if の式が途中で終わる", 0);
    t = &ev[iev++];
    if (is_punct(t, "(")) {
        struct val v = expr_cond();
        if (iev >= nev || !is_punct(&ev[iev], ")"))
            die(ef, el, "#if の ) が要る", 0);
        iev++;
        return v;
    }
    if (t->kind == T_NUM || t->kind == T_CHR)
        return num_value(t);
    if (t->kind == T_IDENT)
        return mkval(0, 0);         /* 定義の無い名前は 0 */
    if (is_punct(t, "-")) {
        struct val v = expr_primary();
        v.v = (i64)(0 - (u64)v.v);
        return v;
    }
    if (is_punct(t, "+"))
        return expr_primary();
    if (is_punct(t, "~")) {
        struct val v = expr_primary();
        v.v = ~v.v;
        return v;
    }
    if (is_punct(t, "!")) {
        struct val v = expr_primary();
        return mkval(v.v == 0, 0);
    }
    die(ef, el, "#if の式に読めない字句がある", t->n->s);
    return mkval(0, 0);
}

static int binprec(struct tok *t)
{
    static const char *ops[] = { "||", "&&", "|", "^", "&", "==", "!=", "<", ">", "<=", ">=",
                                 "<<", ">>", "+", "-", "*", "/", "%", 0 };
    static const int prec[] = { 1, 2, 3, 4, 5, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10 };
    int i;
    if (t->kind != T_PUNCT)
        return 0;
    for (i = 0; ops[i]; i++)
        if (strcmp(t->n->s, ops[i]) == 0)
            return prec[i];
    return 0;
}

static struct val expr_bin(int minprec)
{
    struct val l = expr_primary();
    for (;;) {
        struct tok *op;
        int p;
        struct val r;
        int uns;
        const char *o;
        if (iev >= nev)
            return l;
        op = &ev[iev];
        p = binprec(op);
        if (p == 0 || p < minprec)
            return l;
        iev++;
        o = op->n->s;
        r = expr_bin(p + 1);
        uns = l.uns || r.uns;
        if (strcmp(o, "||") == 0)
            l = mkval(l.v || r.v, 0);
        else if (strcmp(o, "&&") == 0)
            l = mkval(l.v && r.v, 0);
        else if (strcmp(o, "|") == 0)
            l = mkval(l.v | r.v, uns);
        else if (strcmp(o, "^") == 0)
            l = mkval(l.v ^ r.v, uns);
        else if (strcmp(o, "&") == 0)
            l = mkval(l.v & r.v, uns);
        else if (strcmp(o, "==") == 0)
            l = mkval(l.v == r.v, 0);
        else if (strcmp(o, "!=") == 0)
            l = mkval(l.v != r.v, 0);
        else if (strcmp(o, "<") == 0)
            l = mkval(uns ? (u64)l.v < (u64)r.v : l.v < r.v, 0);
        else if (strcmp(o, ">") == 0)
            l = mkval(uns ? (u64)l.v > (u64)r.v : l.v > r.v, 0);
        else if (strcmp(o, "<=") == 0)
            l = mkval(uns ? (u64)l.v <= (u64)r.v : l.v <= r.v, 0);
        else if (strcmp(o, ">=") == 0)
            l = mkval(uns ? (u64)l.v >= (u64)r.v : l.v >= r.v, 0);
        else if (strcmp(o, "<<") == 0)
            l = mkval((i64)((u64)l.v << (r.v & 63)), l.uns);
        else if (strcmp(o, ">>") == 0)
            l = mkval(l.uns ? (i64)((u64)l.v >> (r.v & 63)) : l.v >> (r.v & 63), l.uns);
        else if (strcmp(o, "+") == 0)
            l = mkval((i64)((u64)l.v + (u64)r.v), uns);
        else if (strcmp(o, "-") == 0)
            l = mkval((i64)((u64)l.v - (u64)r.v), uns);
        else if (strcmp(o, "*") == 0)
            l = mkval((i64)((u64)l.v * (u64)r.v), uns);
        else {
            if (r.v == 0)
                die(ef, el, "#if で 0 で割った", 0);
            if (strcmp(o, "/") == 0)
                l = mkval(uns ? (i64)((u64)l.v / (u64)r.v) : l.v / r.v, uns);
            else
                l = mkval(uns ? (i64)((u64)l.v % (u64)r.v) : l.v % r.v, uns);
        }
    }
}

static struct val expr_cond(void)
{
    struct val c = expr_bin(1);
    if (iev < nev && is_punct(&ev[iev], "?")) {
        struct val a, b;
        iev++;
        a = expr_cond();
        if (iev >= nev || !is_punct(&ev[iev], ":"))
            die(ef, el, "#if の ?: の : が要る", 0);
        iev++;
        b = expr_cond();
        if (a.uns || b.uns) {
            a.uns = b.uns = 1;
        }
        return c.v ? a : b;
    }
    return c;
}

/* #if / #elif の行を評価する。defined を先に処理してから展開する */
static int eval_if(struct tok *v, int n, const char *file, int line)
{
    struct tlist pre, post;
    int i;
    struct val r;
    memset(&pre, 0, sizeof pre);
    memset(&post, 0, sizeof post);
    for (i = 0; i < n; i++) {
        if (v[i].kind == T_IDENT && strcmp(v[i].n->s, "defined") == 0) {
            struct tok x;
            int paren = 0;
            struct name *nm;
            i++;
            if (i < n && is_punct(&v[i], "(")) {
                paren = 1;
                i++;
            }
            if (i >= n || v[i].kind != T_IDENT)
                die(file, line, "defined の名前が要る", 0);
            nm = v[i].n;
            if (paren) {
                i++;
                if (i >= n || !is_punct(&v[i], ")"))
                    die(file, line, "defined の ) が要る", 0);
            }
            memset(&x, 0, sizeof x);
            x.kind = T_NUM;
            x.n = intern(nm->macro || (nm->special >= 1 && nm->special <= 3) ? "1" : "0", 1);
            x.noexp = 1;
            tl_add(&pre, &x);
            continue;
        }
        tl_add(&pre, &v[i]);
    }
    expand_list(pre.v, pre.n, &post);
    /* 展開の結果に現れた defined も GCC と同じく評価する */
    for (i = 0; i < post.n; i++)
        if (post.v[i].kind == T_IDENT && strcmp(post.v[i].n->s, "defined") == 0) {
            int ok = eval_if(post.v + i, post.n - i, file, line);
            (void)ok;
            break;
        }
    ev = post.v;
    nev = post.n;
    iev = 0;
    ef = file;
    el = line;
    if (nev == 0)
        die(file, line, "#if の式が無い", 0);
    r = expr_cond();
    if (iev != nev)
        die(file, line, "#if の式の後ろに字句が残る", ev[iev].n->s);
    free(pre.v);
    free(post.v);
    return r.v != 0;
}

static void push_cond(int active, int guard)
{
    if (ncond == ccond) {
        ccond = ccond ? ccond * 2 : 64;
        conds = xrealloc(conds, (long)ccond * (long)sizeof(struct cond));
    }
    conds[ncond].active = active;
    conds[ncond].taken = active;
    conds[ncond].seen_else = 0;
    conds[ncond].guard = guard;
    ncond++;
}

static int parent_active(void)
{
    return ncond < 2 || conds[ncond - 2].active;
}

/* #include のファイルを探す。見つけた経路を返し，*didx に探索の階層の番号 */
static char *find_include(const char *name, int quote, int next_after, int *didx, int *system)
{
    int i;
    char *p;
    *system = 0;
    if (name[0] == '/') {
        FILE *f = fopen(name, "rb");
        if (f) {
            fclose(f);
            *didx = -1;
            return xstrdup(name);
        }
        return 0;
    }
    if (quote && next_after < -1) {
        p = xmalloc((long)strlen(cur->dir) + (long)strlen(name) + 1);
        strcpy(p, cur->dir);
        strcat(p, name);
        {
            FILE *f = fopen(p, "rb");
            if (f) {
                fclose(f);
                *didx = -1;
                return p;
            }
        }
        free(p);
    }
    for (i = next_after >= 0 ? next_after + 1 : 0; i < nidir; i++) {
        FILE *f;
        if (idirs[i].kind == 0 && !quote)
            continue;
        p = xmalloc((long)strlen(idirs[i].path) + (long)strlen(name) + 2);
        strcpy(p, idirs[i].path);
        if (p[0] && p[strlen(p) - 1] != '/')
            strcat(p, "/");
        strcat(p, name);
        f = fopen(p, "rb");
        if (f) {
            fclose(f);
            *didx = i;
            *system = idirs[i].kind == 2;
            return p;
        }
        free(p);
    }
    return 0;
}

static int guarded(const char *path)
{
    struct guard *g;
    for (g = guards; g; g = g->next)
        if (strcmp(g->path, path) == 0)
            return g->macro->macro != 0;
    return 0;
}

/* defer なら依存への記録をファイルを読み始める時まで遅らせる (-include は
 * 積みに後ろから載せるので，読む順と載せる順が逆になる。GCC は読む順に記録する) */
static void include_file(char *path, int didx, int system, int defer)
{
    long len;
    char *buf;
    int dep = dep_kind == 1 || (dep_kind == 2 && !system);
    if (dep && !defer)
        add_dep(path, didx);
    if (guarded(path))
        return;
    buf = readfile(path, &len);
    if (!buf)
        die(cur_file(), cur_line(), "開けない", path);
    push_file(path, buf, len, didx);
    cur->dep_pending = dep && defer;
}

static void d_include(struct src *s, int next)
{
    struct tlist l, e;
    struct tok *t;
    char *name, *path;
    int quote, didx, system;
    long n;
    memset(&l, 0, sizeof l);
    memset(&e, 0, sizeof e);
    read_line(s, &l, 1);
    if (l.n == 0)
        die(s->path, s->line - 1, "#include の名前が要る", 0);
    t = &l.v[0];
    if (t->kind != T_STR) {
        /* マクロで名前を作る形 */
        expand_list(l.v, l.n, &e);
        if (e.n == 0)
            die(s->path, s->line - 1, "#include の名前が要る", 0);
        if (e.v[0].kind == T_STR)
            t = &e.v[0];
        else if (is_punct(&e.v[0], "<")) {
            int i;
            lbn = 0;
            lb_put('<');
            for (i = 1; i < e.n && !is_punct(&e.v[i], ">"); i++) {
                const char *p = e.v[i].n->s;
                if (i > 1 && e.v[i].space)
                    lb_put(' ');
                while (*p)
                    lb_put(*p++);
            }
            lb_put('>');
            {
                struct tok x;
                memset(&x, 0, sizeof x);
                x.kind = T_STR;
                x.n = intern(lb, (int)lbn);
                tl_add(&e, &x);
                t = &e.v[e.n - 1];
            }
        } else
            die(s->path, s->line - 1, "#include の名前が読めない", e.v[0].n->s);
    }
    n = t->n->len;
    quote = t->n->s[0] == '"';
    if (n < 2 || (quote ? t->n->s[n - 1] != '"' : t->n->s[n - 1] != '>'))
        die(s->path, s->line - 1, "#include の名前が読めない", t->n->s);
    name = xstrndup(t->n->s + 1, n - 2);
    path = find_include(name, quote, next ? s->diridx : -2, &didx, &system);
    if (!path)
        die(s->path, s->line - 1, "include のファイルが無い", name);
    free(name);
    free(l.v);
    free(e.v);
    include_file(path, didx, system, 0);
}

static void directive(struct src *s)
{
    struct tok t;
    const char *d;
    int line = s->line;
    lex(s, &t, 0);
    if (t.kind == T_NL || t.kind == T_EOF)
        return;                     /* 空の指令 */
    if (t.kind == T_NUM) {
        /* # 行 "名前" (行の印)。読み捨てる */
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        free(l.v);
        return;
    }
    d = t.n->s;
    if (strcmp(d, "ifdef") == 0 || strcmp(d, "ifndef") == 0 || strcmp(d, "if") == 0) {
        int act = 0, guard = 0;
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (!skipping()) {
            if (d[2] == 'd' || d[2] == 'n') {
                if (l.n < 1 || l.v[0].kind != T_IDENT)
                    die(s->path, line, "#ifdef の名前が要る", 0);
                act = (l.v[0].n->macro != 0 || (l.v[0].n->special >= 1 && l.v[0].n->special <= 3));
                if (d[2] == 'n') {
                    act = !act;
                    if (s->gstate == 0) {
                        s->gstate = 1;
                        s->gname = l.v[0].n;
                        s->gdepth = ncond;
                        guard = 1;
                    }
                }
            } else
                act = eval_if(l.v, l.n, s->path, line);
        }
        if (!guard)
            guard_touch(s);
        push_cond(!skipping() && act, guard);
        free(l.v);
        return;
    }
    if (strcmp(d, "elif") == 0) {
        struct tlist l;
        struct cond *c;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (ncond == 0)
            die(s->path, line, "#if の無い #elif", 0);
        c = &conds[ncond - 1];
        if (c->seen_else)
            die(s->path, line, "#else の後の #elif", 0);
        c->guard = 0;
        if (c->taken || !parent_active())
            c->active = 0;
        else {
            c->active = eval_if(l.v, l.n, s->path, line);
            c->taken = c->active;
        }
        if (s->gstate == 2 && ncond - 1 == s->gdepth)
            s->gstate = -1;
        free(l.v);
        return;
    }
    if (strcmp(d, "else") == 0) {
        struct cond *c;
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        free(l.v);
        if (ncond == 0)
            die(s->path, line, "#if の無い #else", 0);
        c = &conds[ncond - 1];
        if (c->seen_else)
            die(s->path, line, "#else が 2 つある", 0);
        c->seen_else = 1;
        c->active = !c->taken && parent_active();
        c->taken = 1;
        if (s->gstate == 2 && ncond - 1 == s->gdepth)
            s->gstate = -1;
        return;
    }
    if (strcmp(d, "endif") == 0) {
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        free(l.v);
        if (ncond <= s->ncond0)
            die(s->path, line, "#if の無い #endif", 0);
        ncond--;
        if (s->gstate == 2 && ncond == s->gdepth)
            s->gstate = 3;
        return;
    }
    if (skipping()) {
        /* 読み飛ばす枝の中の他の指令は見ない */
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        free(l.v);
        return;
    }
    if (strcmp(d, "define") == 0) {
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (s->gstate == 1) {
            if (l.n >= 1 && l.v[0].n == s->gname)
                s->gstate = 2;
            else
                s->gstate = -1;
        }
        define_macro(l.v, l.n, s->path, line);
        free(l.v);
        return;
    }
    guard_touch(s);
    if (s->gstate == 2 || s->gstate == -1) {
        /* 中の指令は guard の判定に影響しない */
    }
    if (strcmp(d, "undef") == 0) {
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (l.n < 1 || l.v[0].kind != T_IDENT)
            die(s->path, line, "#undef の名前が要る", 0);
        l.v[0].n->macro = 0;
        free(l.v);
        return;
    }
    if (strcmp(d, "include") == 0) {
        d_include(s, 0);
        return;
    }
    if (strcmp(d, "include_next") == 0) {
        d_include(s, 1);
        return;
    }
    if (strcmp(d, "error") == 0) {
        struct tlist l;
        int i;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        fprintf(stderr, "npp1: %s:%d: #error", s->path, line);
        for (i = 0; i < l.n; i++)
            fprintf(stderr, " %s", l.v[i].n->s);
        fprintf(stderr, "\n");
        exit(1);
    }
    if (strcmp(d, "warning") == 0) {
        struct tlist l;
        int i;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        fprintf(stderr, "npp1: %s:%d: 警告: #warning", s->path, line);
        for (i = 0; i < l.n; i++)
            fprintf(stderr, " %s", l.v[i].n->s);
        fprintf(stderr, "\n");
        free(l.v);
        return;
    }
    if (strcmp(d, "pragma") == 0) {
        struct tlist l;
        int i;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (l.n >= 1 && strcmp(l.v[0].n->s, "once") == 0) {
            /* #pragma once: 同じファイルを 2 度読まない */
            struct guard *g = xmalloc(sizeof(struct guard));
            g->path = s->path;
            g->macro = intern("__npp1_pragma_once__", 20);
            if (!g->macro->macro)
                define_cmdline("__npp1_pragma_once__");
            g->next = guards;
            guards = g;
            free(l.v);
            return;
        }
        if (!out_bol)
            putc('\n', out);
        fputs("#pragma", out);
        for (i = 0; i < l.n; i++) {
            putc(' ', out);
            fputs(l.v[i].n->s, out);
        }
        putc('\n', out);
        out_bol = 1;
        have_last = 0;
        free(l.v);
        return;
    }
    if (strcmp(d, "line") == 0 || strcmp(d, "ident") == 0 || strcmp(d, "sccs") == 0) {
        struct tlist l;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        free(l.v);
        return;
    }
    if (asm_mode) {
        /* asm: 指令でない # の行はそのまま出す (# で始まる注釈) */
        struct tlist l;
        int i;
        memset(&l, 0, sizeof l);
        read_line(s, &l, 0);
        if (!out_bol)
            putc('\n', out);
        putc('#', out);
        fputs(t.space ? " " : "", out);
        fputs(t.n->s, out);
        for (i = 0; i < l.n; i++) {
            if (l.v[i].space)
                putc(' ', out);
            fputs(l.v[i].n->s, out);
        }
        putc('\n', out);
        out_bol = 1;
        have_last = 0;
        free(l.v);
        return;
    }
    die(s->path, line, "知らない指令", d);
}

static void push_file(const char *path, char *buf, long len, int diridx)
{
    struct src *s = xmalloc(sizeof(struct src));
    s->path = xstrdup(path);
    s->dir = dirname_of(path);
    s->diridx = diridx;
    s->buf = buf;
    s->len = len;
    s->line = 1;
    s->bol = 1;
    s->ncond0 = ncond;
    s->up = cur;
    cur = s;
    put_marker(s, s->up ? 1 : 0);
}

static void write_deps(void)
{
    FILE *f;
    int i, col;
    if (!dep_kind)
        return;
    if (!dep_file || !dep_target)
        die(0, 0, "依存の記録には -MD / -MMD のファイルと -MT の目標が要る", 0);
    f = fopen(dep_file, "w");
    if (!f)
        die(0, 0, "依存の記録を書けない", dep_file);
    fprintf(f, "%s:", dep_target);
    col = (int)strlen(dep_target) + 1;
    for (i = 0; i < ndeps; i++) {
        int l = (int)strlen(deps[i]);
        if (col + l + 1 > 76) {
            fputs(" \\\n ", f);
            col = 1;
        }
        fprintf(f, " %s", deps[i]);
        col += l + 1;
    }
    fputs("\n", f);
    fclose(f);
}

static void add_idir(const char *p, int kind)
{
    idirs = xrealloc(idirs, (long)(nidir + 1) * (long)sizeof(struct idir));
    idirs[nidir].path = p;
    idirs[nidir].kind = kind;
    nidir++;
}

int main(int argc, char **argv)
{
    const char *input = 0, *outname = 0;
    const char **incs = 0;
    int nincs = 0, i, lang = -1;
    struct ctx c;

    intern("__LINE__", 8)->special = 1;
    intern("__FILE__", 8)->special = 2;
    intern("__COUNTER__", 11)->special = 3;
    intern("_Pragma", 7)->special = 4;
    out = stdout;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *arg = i + 1 < argc ? argv[i + 1] : 0;
#define TAKE(opt) (strcmp(a, opt) == 0 && arg ? (i++, arg) : strncmp(a, opt, strlen(opt)) == 0 && a[strlen(opt)] ? a + strlen(opt) : 0)
        const char *v;
        if ((v = TAKE("-isystem")) != 0)
            add_idir(v, 2);
        else if ((v = TAKE("-iquote")) != 0)
            add_idir(v, 0);
        else if ((v = TAKE("-include")) != 0) {
            incs = xrealloc(incs, (long)(nincs + 1) * (long)sizeof(char *));
            incs[nincs++] = v;
        } else if ((v = TAKE("-I")) != 0)
            add_idir(v, 1);
        else if ((v = TAKE("-D")) != 0)
            define_cmdline(v);
        else if ((v = TAKE("-U")) != 0)
            intern(v, (int)strlen(v))->macro = 0;
        else if ((v = TAKE("-MD")) != 0) {
            dep_kind = 1;
            dep_file = v;
        } else if ((v = TAKE("-MMD")) != 0) {
            dep_kind = 2;
            dep_file = v;
        } else if ((v = TAKE("-MF")) != 0)
            dep_file = v;
        else if ((v = TAKE("-MT")) != 0)
            dep_target = v;
        else if ((v = TAKE("-o")) != 0)
            outname = v;
        else if ((v = TAKE("-x")) != 0) {
            if (strcmp(v, "c") == 0)
                lang = 0;
            else if (strcmp(v, "assembler-with-cpp") == 0)
                lang = 1;
            else
                die(0, 0, "知らない言語", v);
        } else if (strcmp(a, "-P") == 0)
            no_markers = 1;
        else if (strcmp(a, "-nostdinc") == 0 || strcmp(a, "-undef") == 0 || strcmp(a, "-E") == 0)
            ;
        else if (a[0] == '-' && a[1])
            die(0, 0, "知らない選択肢", a);
        else if (!input)
            input = a;
        else
            die(0, 0, "入力が 2 つある", a);
#undef TAKE
    }
    if (!input)
        die(0, 0, "入力が無い", 0);
    if (lang < 0) {
        long l = (long)strlen(input);
        lang = l >= 2 && strcmp(input + l - 2, ".S") == 0;
    }
    asm_mode = lang == 1;
    if (outname) {
        out = fopen(outname, "w");
        if (!out)
            die(0, 0, "書けない", outname);
    }
    if (dep_kind && !dep_target)
        dep_target = outname;
    memset(&c, 0, sizeof c);
    c.file = 1;
    {
        long len;
        char *buf = readfile(input, &len);
        if (!buf)
            die(0, 0, "開けない", input);
        /* - は標準入力。名前は GCC と同じく <stdin> とし，依存に含めない */
        if (strcmp(input, "-") == 0)
            input = "<stdin>";
        else if (dep_kind)
            add_dep(input, -2);
        push_file(input, buf, len, -1);
    }
    /* -include は入力の前に，与えた順に読む。積みの上に後ろから載せる。
     * GCC と同じく，まず作業の場所から探し，無ければ #include "..." の順で探す */
    for (i = nincs - 1; i >= 0; i--) {
        int didx = -2, system = 0;
        char *p = 0;
        FILE *f = fopen(incs[i], "rb");
        if (f) {
            fclose(f);
            p = xstrdup(incs[i]);
        } else
            p = find_include(incs[i], 1, -2, &didx, &system);
        if (!p)
            die(0, 0, "-include のファイルが無い", incs[i]);
        include_file(p, didx, system, 1);
    }
    expand(&c, 0, 1);
    if (!out_bol)
        putc('\n', out);
    if (outname && fclose(out) != 0)
        die(0, 0, "書けない", outname);
    write_deps();
    return 0;
}
