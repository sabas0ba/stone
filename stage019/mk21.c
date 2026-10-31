/* mk21.c --- make の第 21 世代 (Stage 19 の S3。docs/stage019-linux.md 10 章)
 *
 * mk20 (stage017/mk20.c) の全文複製から作り直したものである。Linux 5.4 の
 * Kbuild を読んで回すのに足りなかったものを入れた。mk20 は tcc の Makefile を
 * 読むための大きさの固定された表 (規則 256・変数 512・展開 16 KiB) を持ち，
 * define・eval・export・静的型規則・複数の目標・二次展開・読み込んだ記述の
 * 作り直しを持たなかった。表は動的に確保するように変え，規則と変数の扱いを
 * 作り直した。
 *
 *   mk [-f 記述] [-C 階層] [-I 階層] [-n] [-s] [-k] [-r] [-R] [-B] [-j[N]]
 *      [--no-print-directory] [変数=値...] [目標...]
 *
 * 振舞いは GNU make の文書が定めるものに合わせる。比べる相手はホストの
 * GNU make 4.4.1 である (tools/mk21.sh)。GNU make のソースは参照していない。
 *
 * 持っているもの:
 *
 *   読み    行の継続，注釈，= := ::= += ?=，override・export・unexport，
 *           define/endef，ifeq/ifneq/ifdef/ifndef/else (else ifeq の形)，
 *           include/-include/sinclude (-I の階層も探す)
 *   展開    $(v) ${v} $v $$，名前の展開，置換参照 $(v:a=b) $(v:%.o=%.c)，
 *           関数 (下の表)。eval は展開した文字列を記述として読む
 *   規則    複数の目標，型規則 (複数の目標の型規則は 1 回で全部を作る)，
 *           静的型規則，順序だけの依存 (|)，目標特有と型特有の変数 (依存へ
 *           継がれる)，.PHONY .DELETE_ON_ERROR .SECONDARY .PRECIOUS
 *           .SECONDEXPANSION
 *   探索    VPATH (依存と目標を見つからなければ VPATH の階層で探す)
 *   実行    命令は全行を展開してから 1 行ずつ $(SHELL) -c で走らせる。展開で
 *           改行を含んだ行は改行ごとに別の命令にする。@ - + の頭。$(MAKE) を
 *           含む行は -n でも走らせる。古さは時刻 (ns) で判定する
 *   再帰    $(MAKE) は自身。命令行の変数と選択肢は MAKEFLAGS で子へ渡す。
 *           export した変数・環境から来た変数・命令行の変数は子の環境に入る
 *   作り直し 読み込んだ記述 (include の無かったものを含む) に規則があれば
 *           作り，作ったら最初から読み直す (Kbuild の include/config/auto.conf)
 *
 *   関数    subst patsubst strip findstring filter filter-out sort word
 *           wordlist words firstword lastword dir notdir suffix basename
 *           addsuffix addprefix join wildcard realpath abspath error warning
 *           info shell origin flavor value foreach if or and call eval
 *
 * 組込みの規則と変数は持たない (Kbuild は -rR で消す)。
 *
 * mk20 から引き継いだ決まり:
 *   - 知らない関数は空に展開せず落とす
 *   - $(wildcard) は名前の順に並べる
 *   - 目標が最後まで作られなかったときは `mk: '<目標>' is up to date.` と出す
 *
 * ---- stone の上で ----
 *
 * 命令は今は sh2 に一時ファイルで渡す (sh2 は -c を持たない。S4 の sh が
 * できたら $(SHELL) -c に揃える)。環境は libc27 / kernel31 (S3 の後半) が
 * 入るまで子へ渡らない。MK_HOST を定義するとホストの POSIX (fork・execve・
 * waitpid) で組む。
 *
 * C89 で書く。cc15as と libc26 で stone の OS 向けに，ホストの gcc で
 * ホスト向けに組む。
 *
 * mk20 までの経緯は stage017/mk20.c の頭の註にある。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef MK_HOST
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
extern char **environ;
#else
/* libc26 は getenv を持つ (常に 0 を返す) が stdlib.h に宣言が無い */
char *getenv(char *name);
#endif

typedef unsigned long long mtime_t;

#define MK_VERSION "4.4.1"

/* ---- 小道具 ---- */

static const char *progname = "mk";
static int makelevel;

static void die(const char *fmt, const char *a, const char *b)
{
    fflush(stdout);
    if (makelevel > 0)
        fprintf(stderr, "%s[%d]: ", progname, makelevel);
    else
        fprintf(stderr, "%s: ", progname);
    fprintf(stderr, fmt, a ? a : "", b ? b : "");
    fprintf(stderr, "\n");
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("*** 記憶域が足りない", 0, 0);
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("*** 記憶域が足りない", 0, 0);
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

static int isblank_(int c)
{
    return c == ' ' || c == '\t';
}

static int isspace_(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* 伸びる文字列 */
struct sb {
    char *s;
    size_t n;
    size_t cap;
};

static void sb_need(struct sb *b, size_t more)
{
    if (b->n + more + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->n + more + 1)
            c *= 2;
        b->s = xrealloc(b->s, c);
        b->cap = c;
    }
}

static void sb_addn(struct sb *b, const char *s, size_t n)
{
    sb_need(b, n);
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

static void sb_adds(struct sb *b, const char *s)
{
    sb_addn(b, s, strlen(s));
}

static void sb_addc(struct sb *b, int c)
{
    char ch = (char)c;
    sb_addn(b, &ch, 1);
}

static char *sb_str(struct sb *b)
{
    sb_need(b, 0);
    b->s[b->n] = 0;
    return b->s;
}

static char *sb_take(struct sb *b)
{
    char *s = xstrdup(sb_str(b));
    free(b->s);
    memset(b, 0, sizeof *b);
    return s;
}

/* ---- 表 (文字列から値へ) ---- */

struct hent {
    char *key;
    void *val;
    struct hent *next;
};

struct htab {
    struct hent **b;
    unsigned nb;
    unsigned n;
};

static unsigned hstr(const char *s)
{
    unsigned h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void ht_grow(struct htab *t)
{
    unsigned nb = t->nb ? t->nb * 4 : 64, i;
    struct hent **b = xmalloc(nb * sizeof *b);
    memset(b, 0, nb * sizeof *b);
    for (i = 0; i < t->nb; i++) {
        struct hent *e = t->b[i], *nx;
        for (; e; e = nx) {
            unsigned k = hstr(e->key) & (nb - 1);
            nx = e->next;
            e->next = b[k];
            b[k] = e;
        }
    }
    free(t->b);
    t->b = b;
    t->nb = nb;
}

static void *ht_get(struct htab *t, const char *key)
{
    struct hent *e;
    if (!t->nb)
        return 0;
    for (e = t->b[hstr(key) & (t->nb - 1)]; e; e = e->next)
        if (strcmp(e->key, key) == 0)
            return e->val;
    return 0;
}

static void ht_put(struct htab *t, const char *key, void *val)
{
    struct hent *e;
    unsigned k;
    if (t->n >= t->nb)
        ht_grow(t);
    k = hstr(key) & (t->nb - 1);
    for (e = t->b[k]; e; e = e->next)
        if (strcmp(e->key, key) == 0) {
            e->val = val;
            return;
        }
    e = xmalloc(sizeof *e);
    e->key = xstrdup(key);
    e->val = val;
    e->next = t->b[k];
    t->b[k] = e;
    t->n++;
}

/* ---- 語 ---- */

/* 空白で区切った語を順に取る。*p を進め，語の始まりと長さを返す。無ければ 0 */
static const char *next_word(const char **p, size_t *len)
{
    const char *s = *p, *w;
    while (isspace_((unsigned char)*s))
        s++;
    if (!*s) {
        *p = s;
        return 0;
    }
    w = s;
    while (*s && !isspace_((unsigned char)*s))
        s++;
    *len = (size_t)(s - w);
    *p = s;
    return w;
}

/* 語の並び (関数の結果を組むため) */
struct words {
    char **v;
    int n;
    int cap;
};

static void words_add(struct words *w, const char *s, size_t n)
{
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 16;
        w->v = xrealloc(w->v, (size_t)w->cap * sizeof(char *));
    }
    w->v[w->n++] = xstrndup(s, n);
}

static void words_split(struct words *w, const char *s)
{
    const char *p = s, *t;
    size_t n;
    while ((t = next_word(&p, &n)) != 0)
        words_add(w, t, n);
}

static void words_free(struct words *w)
{
    int i;
    for (i = 0; i < w->n; i++)
        free(w->v[i]);
    free(w->v);
    memset(w, 0, sizeof *w);
}

/* 結果に語を足す (2 つ目からは空白 1 つで継ぐ) */
static void out_word(struct sb *o, int *first, const char *s, size_t n)
{
    if (!*first)
        sb_addc(o, ' ');
    *first = 0;
    sb_addn(o, s, n);
}

/* % を 1 つ含む型 pat に s が合うか。合えば幹を *stem (確保する) に置く */
static int pat_match(const char *pat, const char *s, char **stem)
{
    const char *pc = strchr(pat, '%');
    size_t pl, sl, n = strlen(s);
    if (!pc) {
        if (strcmp(pat, s) != 0)
            return 0;
        if (stem)
            *stem = xstrdup("");
        return 1;
    }
    pl = (size_t)(pc - pat);
    sl = strlen(pc + 1);
    if (n < pl + sl)
        return 0;
    if (strncmp(s, pat, pl) != 0 || strcmp(s + n - sl, pc + 1) != 0)
        return 0;
    if (stem)
        *stem = xstrndup(s + pl, n - pl - sl);
    return 1;
}

/* 型 pat の最初の % を stem に替えたものを o に足す */
static void pat_subst(struct sb *o, const char *pat, const char *stem)
{
    const char *pc = strchr(pat, '%');
    if (!pc) {
        sb_adds(o, pat);
        return;
    }
    sb_addn(o, pat, (size_t)(pc - pat));
    sb_adds(o, stem);
    sb_adds(o, pc + 1);
}

/* ---- 時刻とファイル ---- */

/* ファイルの時刻 (epoch からの ns)。無ければ 0 を返して *exists = 0 */
static mtime_t file_mtime(const char *path, int *exists)
{
    struct stat st;
    if (stat((char *)path, &st) != 0) {
        *exists = 0;
        return 0;
    }
    *exists = 1;
#ifdef MK_HOST
    return (mtime_t)st.st_mtim.tv_sec * 1000000000ull + (mtime_t)st.st_mtim.tv_nsec;
#else
    return ((mtime_t)st.st_mthi << 32) | (mtime_t)st.st_mtlo;
#endif
}

static int path_exists(const char *path)
{
    int e;
    file_mtime(path, &e);
    return e;
}

static int is_dir(const char *path)
{
    struct stat st;
    if (stat((char *)path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

static char *read_file(const char *path, size_t *len)
{
    int fd = open((char *)path, O_RDONLY);
    struct sb b;
    char buf[8192];
    int r;
    if (fd < 0)
        return 0;
    memset(&b, 0, sizeof b);
    while ((r = (int)read(fd, buf, sizeof buf)) > 0)
        sb_addn(&b, buf, (size_t)r);
    close(fd);
    *len = b.n;
    return sb_take(&b);
}

/* 作業の場所 */
static char *cur_dir(void)
{
    char buf[4096];
    if (!getcwd(buf, sizeof buf))
        die("*** 作業の場所が取れない", 0, 0);
    return xstrdup(buf);
}

/* ---- 変数 ---- */

#define FL_RECURSIVE 0
#define FL_SIMPLE 1

/* 出所。数の大きいものが強い (命令行の変数は記述の代入で上書きされない) */
#define OR_DEFAULT 0
#define OR_ENV 1
#define OR_FILE 2
#define OR_CMDLINE 3
#define OR_OVERRIDE 4
#define OR_AUTO 5

#define EX_DEFAULT 0
#define EX_EXPORT 1
#define EX_UNEXPORT 2

struct var {
    char *name;
    char *value;
    int flavor;
    int origin;
    int exp;
    int expanding;              /* 展開している途中 (自分を引く輪の検出) */
};

struct vset {
    struct htab t;
    struct vset *up;
};

static struct vset globals;
static struct vset *ctx = &globals;     /* 今の文脈 (目標特有・call・foreach) */
static int export_all;

static struct var *vs_get(struct vset *s, const char *name)
{
    return ht_get(&s->t, name);
}

static struct var *var_lookup(const char *name)
{
    struct vset *s;
    for (s = ctx; s; s = s->up) {
        struct var *v = vs_get(s, name);
        if (v)
            return v;
    }
    return 0;
}

/* s に name を立てる。既にあれば値を替える (出所と輸出の印は呼び手が決める) */
static struct var *vs_set(struct vset *s, const char *name, const char *value, int flavor, int origin)
{
    struct var *v = vs_get(s, name);
    if (!v) {
        v = xmalloc(sizeof *v);
        memset(v, 0, sizeof *v);
        v->name = xstrdup(name);
        ht_put(&s->t, name, v);
    } else
        free(v->value);
    v->value = xstrdup(value);
    v->flavor = flavor;
    v->origin = origin;
    return v;
}

static struct vset *vs_new(struct vset *up)
{
    struct vset *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->up = up;
    return s;
}

static void vs_free(struct vset *s)
{
    unsigned i;
    for (i = 0; i < s->t.nb; i++) {
        struct hent *e = s->t.b[i], *nx;
        for (; e; e = nx) {
            struct var *v = e->val;
            nx = e->next;
            free(v->name);
            free(v->value);
            free(v);
            free(e->key);
            free(e);
        }
    }
    free(s->t.b);
    free(s);
}

static void expand_into(struct sb *o, const char *s, size_t n);

static char *expand(const char *s)
{
    struct sb o;
    memset(&o, 0, sizeof o);
    expand_into(&o, s, strlen(s));
    return sb_take(&o);
}

static int env_building;        /* 子の環境を組んでいる途中 */
static int env_cycle;           /* その途中で展開の輪に当たった */

/* 変数の値を o に足す (遅延の変数はここで展開する) */
static void var_value_into(struct sb *o, struct var *v)
{
    if (v->flavor == FL_SIMPLE) {
        sb_adds(o, v->value);
        return;
    }
    if (v->expanding) {
        /* 子の環境を組む途中なら，その変数を環境から外すだけにする
         * ($(shell) の中で輸出する変数がその $(shell) の変数自身を引く) */
        if (env_building) {
            env_cycle = 1;
            return;
        }
        die("*** Recursive variable '%s' references itself (eventually).  Stop.", v->name, 0);
    }
    v->expanding = 1;
    expand_into(o, v->value, strlen(v->value));
    v->expanding = 0;
}

/* 代入。op は '=' ':' '+' '?'。記述の代入は強い出所の変数を替えない
 * (override 付きなら替える) */
static void assign(struct vset *s, const char *name, int op, const char *value, int origin, int exp)
{
    struct var *v = vs_get(s, name);
    if (s == &globals && v && v->origin > origin && origin < OR_OVERRIDE)
        goto mark;
    if (op == '?') {
        struct var *o = s == &globals ? v : var_lookup(name);
        if (o)
            goto mark;
        v = vs_set(s, name, value, FL_RECURSIVE, origin);
    } else if (op == ':') {
        char *e = expand(value);
        v = vs_set(s, name, e, FL_SIMPLE, origin);
        free(e);
    } else if (op == '+') {
        struct var *old = s == &globals ? v : var_lookup(name);
        struct sb b;
        if (!old) {
            v = vs_set(s, name, value, FL_RECURSIVE, origin);
            goto mark;
        }
        memset(&b, 0, sizeof b);
        sb_adds(&b, old->value);
        if (old->flavor == FL_SIMPLE) {
            char *e = expand(value);
            if (b.n && *e)
                sb_addc(&b, ' ');
            sb_adds(&b, e);
            free(e);
        } else {
            if (b.n && *value)
                sb_addc(&b, ' ');
            sb_adds(&b, value);
        }
        {
            int fl = old->flavor, org = old->origin > origin ? old->origin : origin;
            int ex = old->exp;
            v = vs_set(s, name, sb_str(&b), fl, org);
            v->exp = ex;
        }
        free(b.s);
    } else
        v = vs_set(s, name, value, FL_RECURSIVE, origin);
mark:
    if (exp && (v = vs_get(s, name)) != 0)
        v->exp = exp;
}

/* 単純な値を全体の表に立てる (make が自分で立てる変数) */
static void set_simple(const char *name, const char *value, int origin)
{
    vs_set(&globals, name, value, FL_SIMPLE, origin);
}

/* ---- 記述の位置 (誤りの報せのため) ---- */

static const char *loc_file;
static int loc_line;

static void fatal_at(const char *msg, const char *arg)
{
    fflush(stdout);
    if (loc_file)
        fprintf(stderr, "%s:%d: *** ", loc_file, loc_line);
    else
        fprintf(stderr, "%s: *** ", progname);
    fprintf(stderr, msg, arg ? arg : "");
    fprintf(stderr, ".  Stop.\n");
    exit(2);
}

/* ---- 展開 ---- */

static int is_fn_name_char(int c)
{
    return (c >= 'a' && c <= 'z') || c == '-';
}

/* s[i] の開き括弧 (open) に合う閉じ括弧の位置。同じ種類の括弧だけを数える */
static size_t match_paren(const char *s, size_t n, size_t i, int open, int close)
{
    int depth = 0;
    for (; i < n; i++) {
        if (s[i] == open)
            depth++;
        else if (s[i] == close) {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return (size_t)-1;
}

/* 深さ 0 の ',' で引数に分ける (どちらの括弧も数える)。max 個目から先は
 * 最後の引数にまとめる */
static int split_args(const char *s, size_t n, int max, const char **av, size_t *al)
{
    int na = 0, depth = 0;
    size_t i, start = 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '(' || c == '{')
            depth++;
        else if (c == ')' || c == '}')
            depth--;
        else if (c == ',' && depth == 0 && na + 1 < max) {
            av[na] = s + start;
            al[na] = i - start;
            na++;
            start = i + 1;
        }
    }
    av[na] = s + start;
    al[na] = n - start;
    return na + 1;
}

static char *expand_n(const char *s, size_t n)
{
    struct sb o;
    memset(&o, 0, sizeof o);
    expand_into(&o, s, n);
    return sb_take(&o);
}

static int call_function(struct sb *o, const char *name, const char *args, size_t alen);

/* 自動変数の D と F の形 ($(@D) など) */
static void auto_df(struct sb *o, const char *base, int which)
{
    const char *p = base, *w;
    size_t n;
    int first = 1;
    while ((w = next_word(&p, &n)) != 0) {
        const char *sl = 0, *q;
        for (q = w; q < w + n; q++)
            if (*q == '/')
                sl = q;
        if (which == 'D') {
            if (!sl)
                out_word(o, &first, ".", 1);
            else if (sl == w)
                out_word(o, &first, "/", 1);
            else
                out_word(o, &first, w, (size_t)(sl - w));
        } else {
            if (!sl)
                out_word(o, &first, w, n);
            else
                out_word(o, &first, sl + 1, n - (size_t)(sl + 1 - w));
        }
    }
}

/* 名前 name の変数を引いて o に足す */
static void ref_var(struct sb *o, const char *name)
{
    struct var *v = var_lookup(name);
    if (v) {
        var_value_into(o, v);
        return;
    }
    if (strlen(name) == 2 && strchr("@<^+?*|%", name[0]) && (name[1] == 'D' || name[1] == 'F')) {
        char b[2];
        b[0] = name[0];
        b[1] = 0;
        v = var_lookup(b);
        if (v)
            auto_df(o, v->value, name[1]);
    }
}

/* 置換参照 $(v:a=b) */
static void subst_ref(struct sb *o, const char *name, const char *from, const char *to)
{
    struct sb val, pf, pt;
    const char *p, *w;
    size_t n;
    int first = 1;
    struct var *v = var_lookup(name);
    memset(&val, 0, sizeof val);
    memset(&pf, 0, sizeof pf);
    memset(&pt, 0, sizeof pt);
    if (v)
        var_value_into(&val, v);
    if (strchr(from, '%')) {
        sb_adds(&pf, from);
        sb_adds(&pt, to);
    } else {
        sb_addc(&pf, '%');
        sb_adds(&pf, from);
        sb_addc(&pt, '%');
        sb_adds(&pt, to);
    }
    p = sb_str(&val);
    while ((w = next_word(&p, &n)) != 0) {
        char *word = xstrndup(w, n), *stem;
        if (pat_match(sb_str(&pf), word, &stem)) {
            struct sb r;
            memset(&r, 0, sizeof r);
            pat_subst(&r, sb_str(&pt), stem);
            out_word(o, &first, r.s ? r.s : "", r.n);
            free(r.s);
            free(stem);
        } else
            out_word(o, &first, w, n);
        free(word);
    }
    free(val.s);
    free(pf.s);
    free(pt.s);
}

/* $(...) の中身 (括弧の内側) を展開する */
static void expand_ref(struct sb *o, const char *s, size_t n)
{
    size_t i = 0, depth;
    /* 関数か: 名前の後に空白 */
    while (i < n && is_fn_name_char((unsigned char)s[i]))
        i++;
    if (i > 0 && i < n && isblank_((unsigned char)s[i])) {
        char *fname = xstrndup(s, i);
        size_t a = i;
        while (a < n && isblank_((unsigned char)s[a]))
            a++;
        if (call_function(o, fname, s + a, n - a)) {
            free(fname);
            return;
        }
        /* 知らない関数は空に展開せず落とす (mk17 からの決まり) */
        fatal_at("unknown function '%s'", fname);
    }
    /* 置換参照か: 深さ 0 の ':' の後に '=' */
    depth = 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '(' || c == '{')
            depth++;
        else if (c == ')' || c == '}')
            depth--;
        else if (c == ':' && depth == 0) {
            size_t j, d2 = 0;
            for (j = i + 1; j < n; j++) {
                char c2 = s[j];
                if (c2 == '(' || c2 == '{')
                    d2++;
                else if (c2 == ')' || c2 == '}')
                    d2--;
                else if (c2 == '=' && d2 == 0)
                    break;
            }
            if (j < n) {
                char *name = expand_n(s, i);
                char *from = expand_n(s + i + 1, j - i - 1);
                char *to = expand_n(s + j + 1, n - j - 1);
                subst_ref(o, name, from, to);
                free(name);
                free(from);
                free(to);
                return;
            }
            break;
        }
    }
    {
        char *name = expand_n(s, n);
        ref_var(o, name);
        free(name);
    }
}

static int expand_depth;

static void expand_into(struct sb *o, const char *s, size_t n)
{
    size_t i = 0;
    if (++expand_depth > 2000)
        fatal_at("展開の入れ子が深すぎる%s", "");
    while (i < n) {
        const char *d = memchr(s + i, '$', n - i);
        size_t k;
        if (!d) {
            sb_addn(o, s + i, n - i);
            break;
        }
        k = (size_t)(d - s);
        sb_addn(o, s + i, k - i);
        i = k + 1;
        if (i >= n)
            break;
        if (s[i] == '$') {
            sb_addc(o, '$');
            i++;
        } else if (s[i] == '(' || s[i] == '{') {
            int open = s[i], close = open == '(' ? ')' : '}';
            size_t j = match_paren(s, n, i, open, close);
            if (j == (size_t)-1)
                fatal_at("unterminated variable reference%s", "");
            expand_ref(o, s + i + 1, j - i - 1);
            i = j + 1;
        } else {
            char name[2];
            name[0] = s[i];
            name[1] = 0;
            ref_var(o, name);
            i++;
        }
    }
    expand_depth--;
}

/* ---- 関数 ---- */

static void eval_text(const char *text, const char *fname, int line);
static char *shell_output(const char *cmd, int *status);
static void glob_into(struct words *out, const char *pat);

static char *arg_exp(const char *s, size_t n)
{
    return expand_n(s, n);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* 経路を絶対の形にし，. と .. と重ねた / を畳む (記号の繋がりは辿らない) */
static char *abs_path(const char *p)
{
    struct sb b;
    struct words parts;
    const char *s;
    int i, n;
    memset(&b, 0, sizeof b);
    memset(&parts, 0, sizeof parts);
    if (p[0] != '/') {
        struct var *cd = vs_get(&globals, "CURDIR");
        char *c = cd ? xstrdup(cd->value) : cur_dir();
        sb_adds(&b, c);
        sb_addc(&b, '/');
        free(c);
    }
    sb_adds(&b, p);
    s = sb_str(&b);
    while (*s) {
        const char *e;
        while (*s == '/')
            s++;
        if (!*s)
            break;
        e = strchr(s, '/');
        if (!e)
            e = s + strlen(s);
        if ((size_t)(e - s) == 1 && s[0] == '.')
            ;
        else if ((size_t)(e - s) == 2 && s[0] == '.' && s[1] == '.') {
            if (parts.n > 0) {
                free(parts.v[parts.n - 1]);
                parts.n--;
            }
        } else
            words_add(&parts, s, (size_t)(e - s));
        s = e;
    }
    free(b.s);
    memset(&b, 0, sizeof b);
    n = parts.n;
    if (n == 0)
        sb_addc(&b, '/');
    for (i = 0; i < n; i++) {
        sb_addc(&b, '/');
        sb_adds(&b, parts.v[i]);
    }
    words_free(&parts);
    return sb_take(&b);
}

static char *real_path(const char *p)
{
#ifdef MK_HOST
    char buf[PATH_MAX];
    if (!realpath(p, buf))
        return 0;
    return xstrdup(buf);
#else
    /* sfs に記号の繋がりは無い。在るものだけを返す */
    char *a = abs_path(p);
    if (!path_exists(a)) {
        free(a);
        return 0;
    }
    return a;
#endif
}

#define NFARG 64

static int call_function(struct sb *o, const char *name, const char *args, size_t alen)
{
    const char *av[NFARG];
    size_t al[NFARG];
    int na, first = 1;

    /* ---- 引数を呼んでから展開するもの ---- */

    if (strcmp(name, "if") == 0) {
        char *c;
        const char *p;
        size_t wn;
        na = split_args(args, alen, 3, av, al);
        if (na < 2)
            fatal_at("insufficient number of arguments (%s) to function 'if'", "1");
        c = arg_exp(av[0], al[0]);
        p = c;
        if (next_word(&p, &wn))
            expand_into(o, av[1], al[1]);
        else if (na > 2)
            expand_into(o, av[2], al[2]);
        free(c);
        return 1;
    }
    if (strcmp(name, "or") == 0 || strcmp(name, "and") == 0) {
        int isor = name[0] == 'o', i;
        char *last = 0;
        na = split_args(args, alen, NFARG, av, al);
        for (i = 0; i < na; i++) {
            char *c = arg_exp(av[i], al[i]);
            const char *p = c;
            size_t wn;
            int nonempty = next_word(&p, &wn) != 0;
            free(last);
            last = c;
            if (isor && nonempty)
                break;
            if (!isor && !nonempty) {
                free(last);
                return 1;
            }
        }
        if (last) {
            const char *p = last;
            size_t wn;
            if (next_word(&p, &wn))
                sb_adds(o, last);
            free(last);
        }
        return 1;
    }
    if (strcmp(name, "foreach") == 0) {
        char *vn, *list;
        const char *p, *w;
        size_t n;
        struct vset *save = ctx, *loop;
        int any = 0;
        na = split_args(args, alen, 3, av, al);
        if (na < 3)
            fatal_at("insufficient number of arguments (%s) to function 'foreach'", "2");
        vn = arg_exp(av[0], al[0]);
        {
            /* 名前の前後の空白を除く */
            const char *q = vn;
            size_t wl;
            const char *w0 = next_word(&q, &wl);
            char *t = w0 ? xstrndup(w0, wl) : xstrdup("");
            free(vn);
            vn = t;
        }
        list = arg_exp(av[1], al[1]);
        loop = vs_new(ctx);
        ctx = loop;
        p = list;
        while ((w = next_word(&p, &n)) != 0) {
            char *word = xstrndup(w, n);
            vs_set(loop, vn, word, FL_SIMPLE, OR_AUTO);
            free(word);
            expand_into(o, av[2], al[2]);
            sb_addc(o, ' ');
            any = 1;
        }
        if (any)
            o->n--, o->s[o->n] = 0;
        ctx = save;
        vs_free(loop);
        free(vn);
        free(list);
        return 1;
    }
    if (strcmp(name, "call") == 0) {
        char *fn;
        struct var *v;
        struct vset *save = ctx, *frame;
        int i;
        na = split_args(args, alen, NFARG, av, al);
        fn = arg_exp(av[0], al[0]);
        {
            const char *q = fn;
            size_t wl;
            const char *w0 = next_word(&q, &wl);
            char *t = w0 ? xstrndup(w0, wl) : xstrdup("");
            free(fn);
            fn = t;
        }
        frame = vs_new(ctx);
        vs_set(frame, "0", fn, FL_SIMPLE, OR_AUTO);
        for (i = 1; i < na; i++) {
            char nb[16], *a = arg_exp(av[i], al[i]);
            sprintf(nb, "%d", i);
            vs_set(frame, nb, a, FL_SIMPLE, OR_AUTO);
            free(a);
        }
        /* 渡さなかった番号は空にする (外側の call の引数を見せない) */
        for (; i < 10; i++) {
            char nb[16];
            sprintf(nb, "%d", i);
            vs_set(frame, nb, "", FL_SIMPLE, OR_AUTO);
        }
        ctx = frame;
        v = var_lookup(fn);
        if (v)
            var_value_into(o, v);
        ctx = save;
        vs_free(frame);
        free(fn);
        return 1;
    }
    if (strcmp(name, "value") == 0) {
        char *vn = arg_exp(args, alen);
        struct var *v = var_lookup(vn);
        if (v)
            sb_adds(o, v->value);
        free(vn);
        return 1;
    }
    if (strcmp(name, "eval") == 0) {
        char *t = arg_exp(args, alen);
        eval_text(t, loc_file, loc_line);
        free(t);
        return 1;
    }

    /* ---- 引数を先に展開するもの ---- */

    if (strcmp(name, "subst") == 0) {
        char *from, *to, *text;
        const char *p, *q;
        size_t fl;
        na = split_args(args, alen, 3, av, al);
        if (na < 3)
            fatal_at("insufficient number of arguments (%s) to function 'subst'", "2");
        from = arg_exp(av[0], al[0]);
        to = arg_exp(av[1], al[1]);
        text = arg_exp(av[2], al[2]);
        fl = strlen(from);
        p = text;
        if (fl == 0)
            sb_adds(o, text);
        else {
            while ((q = strstr(p, from)) != 0) {
                sb_addn(o, p, (size_t)(q - p));
                sb_adds(o, to);
                p = q + fl;
            }
            sb_adds(o, p);
        }
        free(from);
        free(to);
        free(text);
        return 1;
    }
    if (strcmp(name, "patsubst") == 0) {
        char *pat, *rep, *text;
        const char *p, *w;
        size_t n;
        na = split_args(args, alen, 3, av, al);
        if (na < 3)
            fatal_at("insufficient number of arguments (%s) to function 'patsubst'", "2");
        pat = arg_exp(av[0], al[0]);
        rep = arg_exp(av[1], al[1]);
        text = arg_exp(av[2], al[2]);
        /* 型と置き換えの空白はそのまま使う (GNU make と同じ。$(patsubst %, -G %, ..)
         * の置き換えは頭の空白を含む) */
        p = text;
        while ((w = next_word(&p, &n)) != 0) {
            char *word = xstrndup(w, n), *stem;
            if (pat_match(pat, word, &stem)) {
                if (!strchr(pat, '%')) {
                    out_word(o, &first, rep, strlen(rep));
                } else {
                    struct sb r;
                    memset(&r, 0, sizeof r);
                    pat_subst(&r, rep, stem);
                    out_word(o, &first, r.s ? r.s : "", r.n);
                    free(r.s);
                }
                free(stem);
            } else
                out_word(o, &first, w, n);
            free(word);
        }
        free(pat);
        free(rep);
        free(text);
        return 1;
    }
    if (strcmp(name, "filter") == 0 || strcmp(name, "filter-out") == 0) {
        int want = strcmp(name, "filter") == 0;
        char *pats, *text;
        struct words pw;
        const char *p, *w;
        size_t n;
        na = split_args(args, alen, 2, av, al);
        if (na < 2)
            fatal_at("insufficient number of arguments (%s) to function 'filter'", "1");
        pats = arg_exp(av[0], al[0]);
        text = arg_exp(av[1], al[1]);
        memset(&pw, 0, sizeof pw);
        words_split(&pw, pats);
        p = text;
        while ((w = next_word(&p, &n)) != 0) {
            char *word = xstrndup(w, n);
            int i, hit = 0;
            for (i = 0; i < pw.n && !hit; i++)
                hit = pat_match(pw.v[i], word, 0);
            if (hit == want)
                out_word(o, &first, w, n);
            free(word);
        }
        words_free(&pw);
        free(pats);
        free(text);
        return 1;
    }
    if (strcmp(name, "findstring") == 0) {
        char *f, *t;
        na = split_args(args, alen, 2, av, al);
        if (na < 2)
            fatal_at("insufficient number of arguments (%s) to function 'findstring'", "1");
        f = arg_exp(av[0], al[0]);
        t = arg_exp(av[1], al[1]);
        if (strstr(t, f))
            sb_adds(o, f);
        free(f);
        free(t);
        return 1;
    }
    if (strcmp(name, "strip") == 0) {
        char *t = arg_exp(args, alen);
        const char *p = t, *w;
        size_t n;
        while ((w = next_word(&p, &n)) != 0)
            out_word(o, &first, w, n);
        free(t);
        return 1;
    }
    if (strcmp(name, "sort") == 0) {
        char *t = arg_exp(args, alen);
        struct words ws;
        int i;
        memset(&ws, 0, sizeof ws);
        words_split(&ws, t);
        if (ws.n > 1)
            qsort(ws.v, (size_t)ws.n, sizeof(char *), cmp_str);
        for (i = 0; i < ws.n; i++)
            if (i == 0 || strcmp(ws.v[i], ws.v[i - 1]) != 0)
                out_word(o, &first, ws.v[i], strlen(ws.v[i]));
        words_free(&ws);
        free(t);
        return 1;
    }
    if (strcmp(name, "words") == 0 || strcmp(name, "firstword") == 0 || strcmp(name, "lastword") == 0) {
        char *t = arg_exp(args, alen);
        struct words ws;
        memset(&ws, 0, sizeof ws);
        words_split(&ws, t);
        if (name[0] == 'w') {
            char nb[32];
            sprintf(nb, "%d", ws.n);
            sb_adds(o, nb);
        } else if (ws.n > 0)
            sb_adds(o, name[0] == 'f' ? ws.v[0] : ws.v[ws.n - 1]);
        words_free(&ws);
        free(t);
        return 1;
    }
    if (strcmp(name, "word") == 0 || strcmp(name, "wordlist") == 0) {
        int isl = name[4] == 'l', s, e, i;
        char *a0, *a1 = 0, *t;
        struct words ws;
        na = split_args(args, alen, isl ? 3 : 2, av, al);
        if (na < (isl ? 3 : 2))
            fatal_at("insufficient number of arguments to function '%s'", name);
        a0 = arg_exp(av[0], al[0]);
        s = atoi(a0);
        if (isl) {
            a1 = arg_exp(av[1], al[1]);
            e = atoi(a1);
            t = arg_exp(av[2], al[2]);
        } else {
            if (s <= 0)
                fatal_at("first argument to 'word' function must be greater than 0%s", "");
            e = s;
            t = arg_exp(av[1], al[1]);
        }
        memset(&ws, 0, sizeof ws);
        words_split(&ws, t);
        for (i = s; i <= e && i <= ws.n; i++)
            if (i >= 1)
                out_word(o, &first, ws.v[i - 1], strlen(ws.v[i - 1]));
        words_free(&ws);
        free(a0);
        free(a1);
        free(t);
        return 1;
    }
    if (strcmp(name, "dir") == 0 || strcmp(name, "notdir") == 0) {
        char *t = arg_exp(args, alen);
        int isdir = name[0] == 'd';
        const char *p = t, *w;
        size_t n;
        while ((w = next_word(&p, &n)) != 0) {
            const char *sl = 0, *q;
            for (q = w; q < w + n; q++)
                if (*q == '/')
                    sl = q;
            if (isdir) {
                if (sl)
                    out_word(o, &first, w, (size_t)(sl + 1 - w));
                else
                    out_word(o, &first, "./", 2);
            } else {
                if (sl)
                    out_word(o, &first, sl + 1, n - (size_t)(sl + 1 - w));
                else
                    out_word(o, &first, w, n);
            }
        }
        free(t);
        return 1;
    }
    if (strcmp(name, "suffix") == 0 || strcmp(name, "basename") == 0) {
        char *t = arg_exp(args, alen);
        int issuf = name[0] == 's';
        const char *p = t, *w;
        size_t n;
        while ((w = next_word(&p, &n)) != 0) {
            const char *dot = 0, *q;
            for (q = w; q < w + n; q++) {
                if (*q == '.')
                    dot = q;
                else if (*q == '/')
                    dot = 0;
            }
            if (issuf) {
                if (dot)
                    out_word(o, &first, dot, n - (size_t)(dot - w));
            } else
                out_word(o, &first, w, dot ? (size_t)(dot - w) : n);
        }
        free(t);
        return 1;
    }
    if (strcmp(name, "addprefix") == 0 || strcmp(name, "addsuffix") == 0) {
        int pre = name[3] == 'p';
        char *x, *t;
        const char *p, *w;
        size_t n;
        na = split_args(args, alen, 2, av, al);
        if (na < 2)
            fatal_at("insufficient number of arguments (%s) to function 'addprefix'", "1");
        x = arg_exp(av[0], al[0]);
        t = arg_exp(av[1], al[1]);
        p = t;
        while ((w = next_word(&p, &n)) != 0) {
            if (!first)
                sb_addc(o, ' ');
            first = 0;
            if (pre)
                sb_adds(o, x);
            sb_addn(o, w, n);
            if (!pre)
                sb_adds(o, x);
        }
        free(x);
        free(t);
        return 1;
    }
    if (strcmp(name, "join") == 0) {
        char *a, *b;
        struct words wa, wb;
        int i;
        na = split_args(args, alen, 2, av, al);
        a = arg_exp(av[0], al[0]);
        b = na > 1 ? arg_exp(av[1], al[1]) : xstrdup("");
        memset(&wa, 0, sizeof wa);
        memset(&wb, 0, sizeof wb);
        words_split(&wa, a);
        words_split(&wb, b);
        for (i = 0; i < wa.n || i < wb.n; i++) {
            if (!first)
                sb_addc(o, ' ');
            first = 0;
            if (i < wa.n)
                sb_adds(o, wa.v[i]);
            if (i < wb.n)
                sb_adds(o, wb.v[i]);
        }
        words_free(&wa);
        words_free(&wb);
        free(a);
        free(b);
        return 1;
    }
    if (strcmp(name, "wildcard") == 0) {
        char *t = arg_exp(args, alen);
        const char *p = t, *w;
        size_t n;
        while ((w = next_word(&p, &n)) != 0) {
            char *pat = xstrndup(w, n);
            struct words ws;
            int i;
            memset(&ws, 0, sizeof ws);
            glob_into(&ws, pat);
            for (i = 0; i < ws.n; i++)
                out_word(o, &first, ws.v[i], strlen(ws.v[i]));
            words_free(&ws);
            free(pat);
        }
        free(t);
        return 1;
    }
    if (strcmp(name, "realpath") == 0 || strcmp(name, "abspath") == 0) {
        char *t = arg_exp(args, alen);
        int real = name[0] == 'r';
        const char *p = t, *w;
        size_t n;
        while ((w = next_word(&p, &n)) != 0) {
            char *word = xstrndup(w, n);
            char *r = real ? real_path(word) : abs_path(word);
            if (r) {
                out_word(o, &first, r, strlen(r));
                free(r);
            }
            free(word);
        }
        free(t);
        return 1;
    }
    if (strcmp(name, "error") == 0 || strcmp(name, "warning") == 0 || strcmp(name, "info") == 0) {
        char *t = arg_exp(args, alen);
        if (name[0] == 'i') {
            printf("%s\n", t);
            fflush(stdout);
        } else if (name[0] == 'w') {
            fflush(stdout);
            if (loc_file)
                fprintf(stderr, "%s:%d: %s\n", loc_file, loc_line, t);
            else
                fprintf(stderr, "%s\n", t);
        } else
            fatal_at("%s", t);
        free(t);
        return 1;
    }
    if (strcmp(name, "shell") == 0) {
        char *t = arg_exp(args, alen), *r;
        int st;
        size_t n;
        char nb[16];
        r = shell_output(t, &st);
        /* 末尾の改行を除き，残る改行は空白にする */
        n = strlen(r);
        while (n > 0 && r[n - 1] == '\n')
            r[--n] = 0;
        {
            size_t i;
            for (i = 0; i < n; i++)
                if (r[i] == '\n')
                    r[i] = ' ';
        }
        sb_adds(o, r);
        sprintf(nb, "%d", st);
        set_simple(".SHELLSTATUS", nb, OR_AUTO);
        free(r);
        free(t);
        return 1;
    }
    if (strcmp(name, "origin") == 0 || strcmp(name, "flavor") == 0) {
        char *vn = arg_exp(args, alen);
        struct var *v = var_lookup(vn);
        static const char *orig[] = { "default", "environment", "file", "command line", "override", "automatic" };
        if (!v)
            sb_adds(o, "undefined");
        else if (name[0] == 'o')
            sb_adds(o, orig[v->origin]);
        else
            sb_adds(o, v->flavor == FL_SIMPLE ? "simple" : "recursive");
        free(vn);
        return 1;
    }
    return 0;
}

/* ---- ワイルドカード ---- */

static int glob_match(const char *p, const char *t)
{
    for (; *p; p++, t++) {
        if (*p == '*') {
            for (;;) {
                if (glob_match(p + 1, t))
                    return 1;
                if (!*t)
                    return 0;
                t++;
            }
        }
        if (!*t)
            return 0;
        if (*p == '?')
            continue;
        if (*p == '[') {
            const char *q = p + 1;
            int neg = 0, hit = 0;
            if (*q == '!' || *q == '^') {
                neg = 1;
                q++;
            }
            while (*q && *q != ']') {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*t >= q[0] && *t <= q[2])
                        hit = 1;
                    q += 3;
                } else {
                    if (*t == *q)
                        hit = 1;
                    q++;
                }
            }
            if (!*q)
                return 0;
            if (hit == neg)
                return 0;
            p = q;
            continue;
        }
        if (*p == '\\' && p[1])
            p++;
        if (*p != *t)
            return 0;
    }
    return *t == 0;
}

static int has_glob(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] == '*' || s[i] == '?' || s[i] == '[')
            return 1;
    return 0;
}

/* base (末尾に / を持つか空) の下で rest (残りの経路) に合うものを集める */
static void glob_rec(struct words *out, const char *base, const char *rest)
{
    const char *sl = strchr(rest, '/');
    size_t clen = sl ? (size_t)(sl - rest) : strlen(rest);
    struct sb b;
    if (!has_glob(rest, clen)) {
        memset(&b, 0, sizeof b);
        sb_adds(&b, base);
        sb_addn(&b, rest, clen);
        if (sl) {
            sb_addc(&b, '/');
            if (path_exists(sb_str(&b)))
                glob_rec(out, sb_str(&b), sl + 1);
        } else if (path_exists(sb_str(&b)))
            words_add(out, b.s, b.n);
        free(b.s);
        return;
    }
    {
        char *comp = xstrndup(rest, clen);
        DIR *d = opendir(base[0] ? (char *)base : ".");
        struct dirent *e;
        struct words names;
        int i;
        memset(&names, 0, sizeof names);
        if (d) {
            while ((e = readdir(d)) != 0) {
                if (e->d_name[0] == '.' && comp[0] != '.')
                    continue;
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                    continue;
                if (glob_match(comp, e->d_name))
                    words_add(&names, e->d_name, strlen(e->d_name));
            }
            closedir(d);
        }
        if (names.n > 1)
            qsort(names.v, (size_t)names.n, sizeof(char *), cmp_str);
        for (i = 0; i < names.n; i++) {
            memset(&b, 0, sizeof b);
            sb_adds(&b, base);
            sb_adds(&b, names.v[i]);
            if (sl) {
                sb_addc(&b, '/');
                if (is_dir(sb_str(&b)))
                    glob_rec(out, sb_str(&b), sl + 1);
            } else
                words_add(out, b.s, b.n);
            free(b.s);
        }
        words_free(&names);
        free(comp);
    }
}

/* $(wildcard) の 1 語。合うものを名前の順に足す。型でなければ在るかを見る */
static void glob_into(struct words *out, const char *pat)
{
    if (!has_glob(pat, strlen(pat))) {
        if (path_exists(pat))
            words_add(out, pat, strlen(pat));
        return;
    }
    if (pat[0] == '/')
        glob_rec(out, "/", pat + 1);
    else
        glob_rec(out, "", pat);
}

/* ---- ファイルと規則 ---- */

struct cmds {
    char **lines;               /* 展開する前の命令行 */
    int *lnum;                  /* 各行の記述の中の行番号 */
    int n;
    const char *file;
    int line;
};

struct dep {
    char *name;
    struct file *f;
    int oo;                     /* 順序だけ (| の後) */
    struct dep *next;
};

/* 目標特有・型特有の変数の代入 */
struct tvar {
    char *pattern;              /* 型特有なら型。目標特有なら 0 */
    char *name;
    int op;
    char *value;
    int override;
    int exp;
    struct tvar *next;
};

struct prule;

#define ST_NEW 0
#define ST_RUNNING 1
#define ST_DONE 2

struct file {
    char *name;
    char *vpath;                /* VPATH で見つけた経路 */
    struct dep *deps, **dtail;
    struct words deps2;         /* 二次展開する依存 (未展開の文) */
    struct cmds *cmds;
    const char *cmds_file;
    struct tvar *tvars, **tvtail;
    char *stem;                 /* 静的型規則・型規則の幹 */
    struct prule *irule;        /* 選んだ型規則 */
    int is_target;              /* 規則の目標として現れた */
    int phony, precious, secondary;
    int state;
    int status;                 /* 0 成功，1 失敗 */
    int exists;
    int ran;                    /* この走りで命令を走らせた */
    mtime_t mtime;
};

struct prule {
    struct words targets;       /* % を含む目標 */
    char *deps;                 /* 一次展開した依存の文 (% を含む) */
    char *oo;                   /* 順序だけの依存の文 */
    int second;                 /* 二次展開する */
    struct cmds *cmds;
    struct prule *next;
};

static struct htab files;
static struct prule *prules, **prtail = &prules;
static struct tvar *pvars, **pvtail = &pvars;   /* 型特有の変数 */
static int second_expansion;
static int delete_on_error;
static int all_secondary;
static char *default_goal;

/* 名前の頭の ./ を除く (GNU make と同じく ./a と a を同じファイルとする) */
static const char *norm_name(const char *name)
{
    while (name[0] == '.' && name[1] == '/' && name[2]) {
        name += 2;
        while (name[0] == '/' && name[1])
            name++;
    }
    return name;
}

static struct file *file_get(const char *name)
{
    return ht_get(&files, norm_name(name));
}

static struct file *file_enter(const char *name)
{
    struct file *f;
    name = norm_name(name);
    f = ht_get(&files, name);
    if (f)
        return f;
    f = xmalloc(sizeof *f);
    memset(f, 0, sizeof *f);
    f->name = xstrdup(name);
    f->dtail = &f->deps;
    f->tvtail = &f->tvars;
    ht_put(&files, name, f);
    return f;
}

static void add_dep(struct file *f, const char *name, int oo)
{
    struct dep *d;
    name = norm_name(name);
    for (d = f->deps; d; d = d->next)
        if (d->oo == oo && strcmp(d->name, name) == 0)
            return;
    d = xmalloc(sizeof *d);
    memset(d, 0, sizeof *d);
    d->name = xstrdup(name);
    d->f = file_enter(name);
    d->oo = oo;
    *f->dtail = d;
    f->dtail = &d->next;
}

/* ---- 輸出の印 ---- */

static struct htab expmarks;    /* 名前 -> EX_EXPORT / EX_UNEXPORT */

static void mark_export(const char *name, int how)
{
    static int marks[3] = { 0, EX_EXPORT, EX_UNEXPORT };
    ht_put(&expmarks, name, &marks[how]);
}

static int export_mark(const char *name)
{
    int *m = ht_get(&expmarks, name);
    return m ? *m : EX_DEFAULT;
}

/* ---- 記述の読み ---- */

struct makefile {
    char *name;                 /* include に書かれた名前 */
    char *path;                 /* 読んだ経路 (無ければ 0) */
    int optional;               /* -include */
};

static struct makefile *mfiles;
static int nmfiles, cmfiles;
static struct words incdirs;    /* -I */

#define NCOND 64

struct cond {
    int active;                 /* この枝を読む */
    int taken;                  /* どれかの枝を読んだ */
    int outer;                  /* 外側が読む状態だった */
};

struct pstate {
    const char *fname;
    int line;
    struct cond conds[NCOND];
    int ncond;
    /* 規則の文脈 (命令行を受ける目標) */
    int in_rule;
    struct file **rt;
    struct dep ***rmark;        /* 各目標の，この規則が依存を足す前の末尾 */
    int nrt;
    struct prule *rp;
    struct cmds *rc;
};

static void read_makefile(const char *name, int optional);

static int skipping(struct pstate *ps)
{
    return ps->ncond > 0 && !ps->conds[ps->ncond - 1].active;
}

static void end_rule(struct pstate *ps)
{
    ps->in_rule = 0;
    free(ps->rt);
    ps->rt = 0;
    free(ps->rmark);
    ps->rmark = 0;
    ps->nrt = 0;
    ps->rp = 0;
    ps->rc = 0;
}

/* 規則の命令を受ける器を作り，目標に付ける */
static struct cmds *rule_cmds(struct pstate *ps)
{
    int i;
    if (ps->rc)
        return ps->rc;
    ps->rc = xmalloc(sizeof *ps->rc);
    memset(ps->rc, 0, sizeof *ps->rc);
    ps->rc->file = ps->fname;
    ps->rc->line = ps->line;
    if (ps->rp)
        ps->rp->cmds = ps->rc;
    for (i = 0; i < ps->nrt; i++) {
        struct file *f = ps->rt[i];
        if (f->cmds && f->cmds != ps->rc) {
            fflush(stdout);
            fprintf(stderr, "%s:%d: warning: overriding recipe for target '%s'\n", ps->fname, ps->line, f->name);
            fprintf(stderr, "%s:%d: warning: ignoring old recipe for target '%s'\n", f->cmds->file, f->cmds->line, f->name);
        }
        f->cmds = ps->rc;
        /* 命令を持つ規則の依存を先頭に移す ($< はその最初の依存) */
        if (ps->rmark && ps->rmark[i] && *ps->rmark[i] && ps->rmark[i] != &f->deps) {
            struct dep *seg = *ps->rmark[i], *last = seg;
            while (last->next)
                last = last->next;
            *ps->rmark[i] = 0;
            last->next = f->deps;
            f->deps = seg;
            f->dtail = ps->rmark[i];
        }
    }
    return ps->rc;
}

static void cmds_add(struct cmds *c, const char *line, int lnum)
{
    c->lines = xrealloc(c->lines, (size_t)(c->n + 1) * sizeof(char *));
    c->lnum = xrealloc(c->lnum, (size_t)(c->n + 1) * sizeof(int));
    c->lnum[c->n] = lnum;
    c->lines[c->n++] = xstrdup(line);
}

/* 前後の空白を除いた写し */
static char *trim(const char *s, size_t n)
{
    while (n > 0 && isspace_((unsigned char)*s))
        s++, n--;
    while (n > 0 && isspace_((unsigned char)s[n - 1]))
        n--;
    return xstrndup(s, n);
}

/* 注釈を除く ('\#' は '#' にする) */
static void strip_comment(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' && r[1] == '#') {
            *w++ = '#';
            r += 2;
            continue;
        }
        if (*r == '#')
            break;
        *w++ = *r++;
    }
    *w = 0;
}

/* 語 kw で始まり，その後が空白か終わりか */
static const char *starts_kw(const char *s, const char *kw)
{
    size_t n = strlen(kw);
    if (strncmp(s, kw, n) != 0)
        return 0;
    if (s[n] && !isblank_((unsigned char)s[n]))
        return 0;
    s += n;
    while (isblank_((unsigned char)*s))
        s++;
    return s;
}

/* ifeq / ifneq の引数を 2 つ取る。(a,b) か 'a' 'b' か "a" "b" */
static int cond_args(const char *s, char **a, char **b)
{
    if (*s == '(') {
        size_t n = strlen(s), i, depth = 0, comma = 0, close = 0;
        for (i = 0; i < n; i++) {
            if (s[i] == '(')
                depth++;
            else if (s[i] == ')') {
                depth--;
                if (depth == 0) {
                    close = i;
                    break;
                }
            } else if (s[i] == ',' && depth == 1 && !comma)
                comma = i;
        }
        if (!comma || !close)
            return 0;
        {
            char *ra = trim(s + 1, comma - 1), *rb = trim(s + comma + 1, close - comma - 1);
            *a = expand(ra);
            *b = expand(rb);
            free(ra);
            free(rb);
        }
        return 1;
    }
    if (*s == '\'' || *s == '"') {
        const char *e = strchr(s + 1, *s), *t;
        char *ra, *rb;
        if (!e)
            return 0;
        ra = xstrndup(s + 1, (size_t)(e - s - 1));
        t = e + 1;
        while (isblank_((unsigned char)*t))
            t++;
        if (*t != '\'' && *t != '"') {
            free(ra);
            return 0;
        }
        e = strchr(t + 1, *t);
        if (!e) {
            free(ra);
            return 0;
        }
        rb = xstrndup(t + 1, (size_t)(e - t - 1));
        *a = expand(ra);
        *b = expand(rb);
        free(ra);
        free(rb);
        return 1;
    }
    return 0;
}

/* 条件の指令なら処理して 1 を返す */
static int conditional(struct pstate *ps, const char *line)
{
    const char *r;
    int isnot = 0, kind = 0;
    if ((r = starts_kw(line, "ifeq")) != 0)
        kind = 1;
    else if ((r = starts_kw(line, "ifneq")) != 0)
        kind = 1, isnot = 1;
    else if ((r = starts_kw(line, "ifdef")) != 0)
        kind = 2;
    else if ((r = starts_kw(line, "ifndef")) != 0)
        kind = 2, isnot = 1;
    else if ((r = starts_kw(line, "else")) != 0) {
        struct cond *c;
        if (ps->ncond == 0)
            fatal_at("extraneous 'else'%s", "");
        c = &ps->conds[ps->ncond - 1];
        if (*r == 0) {
            c->active = c->outer && !c->taken;
            if (c->active)
                c->taken = 1;
            return 1;
        }
        /* else ifeq ... */
        if (!c->outer || c->taken) {
            c->active = 0;
            return 1;
        }
        {
            /* 条件を評価して，この入れ子の段の状態を替える */
            struct pstate tmp;
            int res;
            memset(&tmp, 0, sizeof tmp);
            tmp.fname = ps->fname;
            tmp.line = ps->line;
            if (!conditional(&tmp, r) || tmp.ncond != 1)
                fatal_at("extraneous text after 'else' directive%s", "");
            res = tmp.conds[0].active;
            c->active = res;
            if (res)
                c->taken = 1;
        }
        return 1;
    } else if ((r = starts_kw(line, "endif")) != 0) {
        if (ps->ncond == 0)
            fatal_at("extraneous 'endif'%s", "");
        ps->ncond--;
        return 1;
    } else
        return 0;
    {
        struct cond *c;
        int outer = !skipping(ps), res = 0;
        if (ps->ncond >= NCOND)
            fatal_at("条件の入れ子が深すぎる%s", "");
        if (outer) {
            if (kind == 1) {
                char *a, *b;
                if (!cond_args(r, &a, &b))
                    fatal_at("invalid syntax in conditional%s", "");
                res = strcmp(a, b) == 0;
                free(a);
                free(b);
            } else {
                char *n = expand(r), *t;
                struct var *v;
                t = trim(n, strlen(n));
                v = var_lookup(t);
                res = v && v->value[0] != 0;
                free(n);
                free(t);
            }
            if (isnot)
                res = !res;
        }
        c = &ps->conds[ps->ncond++];
        c->outer = outer;
        c->active = outer && res;
        c->taken = c->active;
    }
    return 1;
}

/* 深さ 0 で最初の代入の演算子か規則の ':' を探す。
 * 代入なら *op ('=' ':' '+' '?' '!') と名前の終わりを，規則なら ':' の位置を返す */
#define FIND_NONE 0
#define FIND_ASSIGN 1
#define FIND_RULE 2

static int find_op(const char *s, size_t *pos, int *op, size_t *vend)
{
    size_t i, n = strlen(s);
    int depth = 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '(' || c == '{')
            depth++;
        else if (c == ')' || c == '}')
            depth--;
        else if (depth == 0 && c == '=') {
            *pos = i + 1;
            if (i > 0 && (s[i - 1] == '+' || s[i - 1] == '?' || s[i - 1] == '!')) {
                *op = s[i - 1];
                *vend = i - 1;
            } else if (i > 0 && s[i - 1] == ':') {
                *op = ':';
                *vend = i - 1;
                if (i > 1 && s[i - 2] == ':')
                    *vend = i - 2;
            } else {
                *op = '=';
                *vend = i;
            }
            return FIND_ASSIGN;
        } else if (depth == 0 && c == ':') {
            if (s[i + 1] == '=')
                continue;
            if (s[i + 1] == ':' && s[i + 2] == '=')
                continue;
            *pos = i;
            return FIND_RULE;
        }
    }
    return FIND_NONE;
}

/* 代入の行 (頭の override / export などを含む)。sv が 0 でなければ目標特有の
 * 代入として sv に積む */
static void do_assign(struct pstate *ps, const char *line, struct file **tf, int ntf, const char *tpat)
{
    const char *p = line, *r;
    int override = 0, exp = EX_DEFAULT, op, kind;
    size_t pos, vend;
    char *name, *value;
    for (;;) {
        if ((r = starts_kw(p, "override")) != 0) {
            override = 1;
            p = r;
        } else if ((r = starts_kw(p, "export")) != 0) {
            exp = EX_EXPORT;
            p = r;
        } else if ((r = starts_kw(p, "unexport")) != 0) {
            exp = EX_UNEXPORT;
            p = r;
        } else if ((r = starts_kw(p, "private")) != 0)
            p = r;
        else
            break;
    }
    kind = find_op(p, &pos, &op, &vend);
    if (kind != FIND_ASSIGN)
        fatal_at("missing separator%s", "");
    {
        char *raw = trim(p, vend);
        name = expand(raw);
        free(raw);
        raw = name;
        name = trim(raw, strlen(raw));
        free(raw);
    }
    r = p + pos;
    while (isblank_((unsigned char)*r))
        r++;
    value = xstrdup(r);
    if (op == '!') {
        char *cmd = expand(value);
        int st;
        free(value);
        value = shell_output(cmd, &st);
        {
            size_t n = strlen(value), i;
            while (n > 0 && value[n - 1] == '\n')
                value[--n] = 0;
            for (i = 0; i < n; i++)
                if (value[i] == '\n')
                    value[i] = ' ';
        }
        free(cmd);
        op = '=';
    }
    if (tf || tpat) {
        int i;
        for (i = 0; i < (tpat ? 1 : ntf); i++) {
            struct tvar *t = xmalloc(sizeof *t);
            memset(t, 0, sizeof *t);
            t->pattern = tpat ? xstrdup(tpat) : 0;
            t->name = xstrdup(name);
            t->op = op;
            t->value = xstrdup(value);
            t->override = override;
            t->exp = exp;
            if (tpat) {
                *pvtail = t;
                pvtail = &t->next;
            } else {
                *tf[i]->tvtail = t;
                tf[i]->tvtail = &t->next;
            }
        }
    } else {
        assign(&globals, name, op, value, override ? OR_OVERRIDE : OR_FILE, 0);
        if (exp)
            mark_export(name, exp);
    }
    (void)ps;
    free(name);
    free(value);
}

static int is_special_target(const char *t)
{
    return t[0] == '.' && strchr(t, '/') == 0 && t[1] >= 'A' && t[1] <= 'Z';
}

static void special_target(const char *t, struct words *deps)
{
    int i;
    if (strcmp(t, ".PHONY") == 0) {
        for (i = 0; i < deps->n; i++)
            file_enter(deps->v[i])->phony = 1;
    } else if (strcmp(t, ".PRECIOUS") == 0) {
        for (i = 0; i < deps->n; i++)
            file_enter(deps->v[i])->precious = 1;
    } else if (strcmp(t, ".SECONDARY") == 0) {
        if (deps->n == 0)
            all_secondary = 1;
        for (i = 0; i < deps->n; i++)
            file_enter(deps->v[i])->secondary = 1;
    } else if (strcmp(t, ".DELETE_ON_ERROR") == 0)
        delete_on_error = 1;
    else if (strcmp(t, ".SECONDEXPANSION") == 0)
        second_expansion = 1;
    else if (strcmp(t, ".EXPORT_ALL_VARIABLES") == 0)
        export_all = 1;
    /* .SUFFIXES .DEFAULT .INTERMEDIATE .NOTPARALLEL .SILENT .IGNORE ほかは受けて
     * 何もしない (Kbuild は使わない) */
}

/* 依存の文 (一次展開済み) を d と oo に分ける */
static void split_deps(const char *text, struct words *d, struct words *oo)
{
    const char *p = text, *w;
    size_t n;
    int inoo = 0;
    while ((w = next_word(&p, &n)) != 0) {
        if (n == 1 && w[0] == '|') {
            inoo = 1;
            continue;
        }
        words_add(inoo ? oo : d, w, n);
    }
}

/* 規則の行 */
static void do_rule(struct pstate *ps, const char *line, size_t colon)
{
    char *traw = xstrndup(line, colon), *texp, *rest, *recipe = 0;
    struct words targets, dw, ow;
    const char *r;
    int dc = 0, i, kind, op;
    size_t pos, vend, k;

    end_rule(ps);
    texp = expand(traw);
    free(traw);
    memset(&targets, 0, sizeof targets);
    words_split(&targets, texp);
    free(texp);
    r = line + colon + 1;
    if (*r == ':') {
        dc = 1;
        r++;
    }
    (void)dc;
    rest = xstrdup(r);
    /* 目標特有の変数か */
    kind = find_op(rest, &pos, &op, &vend);
    if (kind == FIND_ASSIGN) {
        int pattern = 0;
        struct file **tf;
        for (i = 0; i < targets.n; i++)
            if (strchr(targets.v[i], '%'))
                pattern = 1;
        if (pattern) {
            for (i = 0; i < targets.n; i++)
                do_assign(ps, rest, 0, 0, norm_name(targets.v[i]));
        } else {
            tf = xmalloc((size_t)(targets.n + 1) * sizeof *tf);
            for (i = 0; i < targets.n; i++)
                tf[i] = file_enter(targets.v[i]);
            do_assign(ps, rest, tf, targets.n, 0);
            free(tf);
        }
        free(rest);
        words_free(&targets);
        return;
    }
    /* 命令が同じ行にあるか (深さ 0 の ';') */
    {
        size_t n = strlen(rest);
        int depth = 0;
        for (k = 0; k < n; k++) {
            if (rest[k] == '(' || rest[k] == '{')
                depth++;
            else if (rest[k] == ')' || rest[k] == '}')
                depth--;
            else if (rest[k] == ';' && depth == 0) {
                recipe = xstrdup(rest + k + 1);
                rest[k] = 0;
                break;
            }
        }
    }
    memset(&dw, 0, sizeof dw);
    memset(&ow, 0, sizeof ow);
    ps->in_rule = 1;
    ps->rt = xmalloc((size_t)(targets.n + 1) * sizeof(struct file *));
    ps->rmark = xmalloc((size_t)(targets.n + 1) * sizeof(struct dep **));
    ps->nrt = 0;
    /* 静的型規則か (残りに深さ 0 の ':' がある) */
    kind = find_op(rest, &pos, &op, &vend);
    if (kind == FIND_RULE) {
        char *tpat_raw = trim(rest, pos), *tpat = expand(tpat_raw);
        char *dpat_raw = xstrdup(rest + pos + 1), *dpat = expand(dpat_raw);
        char *tp0 = trim(tpat, strlen(tpat)), *tp = xstrdup(norm_name(tp0));
        split_deps(dpat, &dw, &ow);
        for (i = 0; i < targets.n; i++) {
            char *stem;
            struct file *f;
            int j;
            if (!pat_match(tp, norm_name(targets.v[i]), &stem)) {
                fflush(stdout);
                fprintf(stderr, "%s:%d: target '%s' doesn't match the target pattern\n", ps->fname, ps->line, targets.v[i]);
                continue;
            }
            f = file_enter(targets.v[i]);
            f->is_target = 1;
            free(f->stem);
            f->stem = stem;
            ps->rmark[ps->nrt] = f->dtail;
            if (second_expansion && strchr(dpat, '$')) {
                struct sb b;
                memset(&b, 0, sizeof b);
                /* 型の % を幹に替えてから二次展開に回す */
                {
                    const char *p = dpat, *w;
                    size_t n;
                    int first = 1;
                    while ((w = next_word(&p, &n)) != 0) {
                        char *word = xstrndup(w, n);
                        if (!first)
                            sb_addc(&b, ' ');
                        first = 0;
                        pat_subst(&b, word, stem);
                        free(word);
                    }
                }
                words_add(&f->deps2, b.s ? b.s : "", b.n);
                free(b.s);
            } else {
                for (j = 0; j < dw.n; j++) {
                    struct sb b;
                    memset(&b, 0, sizeof b);
                    pat_subst(&b, dw.v[j], stem);
                    add_dep(f, sb_str(&b), 0);
                    free(b.s);
                }
                for (j = 0; j < ow.n; j++) {
                    struct sb b;
                    memset(&b, 0, sizeof b);
                    pat_subst(&b, ow.v[j], stem);
                    add_dep(f, sb_str(&b), 1);
                    free(b.s);
                }
            }
            ps->rt[ps->nrt++] = f;
            if (!default_goal && (f->name[0] != '.' || strchr(f->name, '/')))
                default_goal = xstrdup(f->name);
        }
        free(tpat_raw);
        free(tpat);
        free(tp);
        free(tp0);
        free(dpat_raw);
        free(dpat);
    } else {
        char *dexp = expand(rest);
        int pattern = 0;
        split_deps(dexp, &dw, &ow);
        for (i = 0; i < targets.n; i++)
            if (strchr(targets.v[i], '%'))
                pattern = 1;
        if (pattern) {
            struct prule *pr = xmalloc(sizeof *pr);
            memset(pr, 0, sizeof *pr);
            for (i = 0; i < targets.n; i++) {
                const char *t = norm_name(targets.v[i]);
                words_add(&pr->targets, t, strlen(t));
            }
            {
                struct sb b, c;
                memset(&b, 0, sizeof b);
                memset(&c, 0, sizeof c);
                for (i = 0; i < dw.n; i++) {
                    if (i)
                        sb_addc(&b, ' ');
                    sb_adds(&b, dw.v[i]);
                }
                for (i = 0; i < ow.n; i++) {
                    if (i)
                        sb_addc(&c, ' ');
                    sb_adds(&c, ow.v[i]);
                }
                pr->deps = sb_take(&b);
                pr->oo = sb_take(&c);
            }
            pr->second = second_expansion && strchr(dexp, '$') != 0;
            *prtail = pr;
            prtail = &pr->next;
            ps->rp = pr;
        } else {
            for (i = 0; i < targets.n; i++) {
                const char *t = targets.v[i];
                struct file *f;
                int j;
                if (is_special_target(t)) {
                    struct words all;
                    memset(&all, 0, sizeof all);
                    for (j = 0; j < dw.n; j++)
                        words_add(&all, dw.v[j], strlen(dw.v[j]));
                    special_target(t, &all);
                    words_free(&all);
                    continue;
                }
                f = file_enter(t);
                f->is_target = 1;
                ps->rmark[ps->nrt] = f->dtail;
                if (second_expansion && strchr(dexp, '$'))
                    words_add(&f->deps2, dexp, strlen(dexp));
                else {
                    for (j = 0; j < dw.n; j++)
                        add_dep(f, dw.v[j], 0);
                    for (j = 0; j < ow.n; j++)
                        add_dep(f, ow.v[j], 1);
                }
                ps->rt[ps->nrt++] = f;
                if (!default_goal && (t[0] != '.' || strchr(t, '/')))
                    default_goal = xstrdup(t);
            }
        }
        free(dexp);
    }
    if (recipe) {
        const char *rr = recipe;
        while (isblank_((unsigned char)*rr))
            rr++;
        cmds_add(rule_cmds(ps), rr, ps->line);
        free(recipe);
    }
    words_free(&dw);
    words_free(&ow);
    words_free(&targets);
    free(rest);
}

/* include の行 */
static void do_include(const char *rest, int optional)
{
    char *e = expand(rest);
    const char *p = e, *w;
    size_t n;
    while ((w = next_word(&p, &n)) != 0) {
        char *name = xstrndup(w, n);
        struct words g;
        int i;
        memset(&g, 0, sizeof g);
        if (has_glob(name, n))
            glob_into(&g, name);
        else
            words_add(&g, name, n);
        for (i = 0; i < g.n; i++)
            read_makefile(g.v[i], optional);
        words_free(&g);
        free(name);
    }
    free(e);
}

/* export / unexport の行 (代入を伴わないもの) */
static void do_export(const char *rest, int how)
{
    char *e = expand(rest);
    const char *p = e, *w;
    size_t n;
    int any = 0;
    while ((w = next_word(&p, &n)) != 0) {
        char *name = xstrndup(w, n);
        mark_export(name, how);
        free(name);
        any = 1;
    }
    if (!any)
        export_all = how == EX_EXPORT;
    free(e);
}

/* 行の始まりが命令の頭 (TAB) */
#define RECIPE_PREFIX '\t'

/* 文字列 buf (長さ len) を記述として読む */
static void parse_buffer(const char *buf, size_t len, const char *fname, int line0)
{
    struct pstate ps;
    const char *p = buf, *end = buf + len;
    const char *save_file = loc_file;
    int save_line = loc_line;
    /* define を読んでいる途中 */
    int in_define = 0, define_depth = 0, def_override = 0, def_exp = 0, def_op = '=';
    char *def_name = 0;
    struct sb def_body;
    int lineno = line0 - 1;

    memset(&ps, 0, sizeof ps);
    memset(&def_body, 0, sizeof def_body);
    ps.fname = fname;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        struct sb lb;
        int start_line;
        lineno++;
        start_line = lineno;
        ps.line = lineno;
        loc_file = fname;
        loc_line = lineno;

        if (in_define) {
            char *t = trim(p, (size_t)(le - p));
            const char *r;
            if ((r = starts_kw(t, "endef")) != 0 && (*r == 0 || *r == '#')) {
                if (define_depth == 0) {
                    in_define = 0;
                    if (!skipping(&ps)) {
                        char *v = sb_take(&def_body);
                        if (def_op == '+' || def_op == '?' || def_op == ':' || def_op == '=')
                            assign(&globals, def_name, def_op, v, def_override ? OR_OVERRIDE : OR_FILE, 0);
                        if (def_exp)
                            mark_export(def_name, def_exp);
                        free(v);
                    } else {
                        free(def_body.s);
                        memset(&def_body, 0, sizeof def_body);
                    }
                    free(def_name);
                    def_name = 0;
                    free(t);
                    p = nl ? nl + 1 : end;
                    continue;
                }
                define_depth--;
            } else if (starts_kw(t, "define") || (strncmp(t, "define", 6) == 0 && t[6] == 0))
                define_depth++;
            free(t);
            if (def_body.n || def_body.s) {
                /* 前の行が \ で終わっていれば，通常の行と同じく空白 1 つで継ぐ
                 * (GNU make と同じ。命令として使う時も継いだ形になる) */
                size_t bs = 0;
                while (bs < def_body.n && def_body.s[def_body.n - 1 - bs] == '\\')
                    bs++;
                if (bs % 2 == 1) {
                    const char *q = p;
                    def_body.n--;
                    while (def_body.n > 0 && isblank_((unsigned char)def_body.s[def_body.n - 1]))
                        def_body.n--;
                    def_body.s[def_body.n] = 0;
                    while (q < le && isblank_((unsigned char)*q))
                        q++;
                    sb_addc(&def_body, ' ');
                    sb_addn(&def_body, q, (size_t)(le - q));
                    p = nl ? nl + 1 : end;
                    continue;
                }
                sb_addc(&def_body, '\n');
            }
            sb_addn(&def_body, p, (size_t)(le - p));
            if (!def_body.s)
                sb_need(&def_body, 0);
            p = nl ? nl + 1 : end;
            continue;
        }

        /* 命令行 */
        if (*p == RECIPE_PREFIX && ps.in_rule) {
            memset(&lb, 0, sizeof lb);
            sb_addn(&lb, p + 1, (size_t)(le - p - 1));
            p = nl ? nl + 1 : end;
            /* 継続は \ と改行を残し，次の行の頭の TAB を 1 つ除く */
            for (;;) {
                size_t bs = 0;
                while (bs < lb.n && lb.s[lb.n - 1 - bs] == '\\')
                    bs++;
                if (bs % 2 == 0 || p >= end)
                    break;
                nl = memchr(p, '\n', (size_t)(end - p));
                le = nl ? nl : end;
                lineno++;
                sb_addc(&lb, '\n');
                if (*p == RECIPE_PREFIX)
                    sb_addn(&lb, p + 1, (size_t)(le - p - 1));
                else
                    sb_addn(&lb, p, (size_t)(le - p));
                p = nl ? nl + 1 : end;
            }
            if (!skipping(&ps))
                cmds_add(rule_cmds(&ps), sb_str(&lb), start_line);
            free(lb.s);
            continue;
        }

        /* 記述の行。継続は空白 1 つにまとめる */
        memset(&lb, 0, sizeof lb);
        sb_addn(&lb, p, (size_t)(le - p));
        p = nl ? nl + 1 : end;
        for (;;) {
            size_t bs = 0;
            while (bs < lb.n && lb.s[lb.n - 1 - bs] == '\\')
                bs++;
            if (bs % 2 == 0 || p >= end)
                break;
            lb.n--;
            while (lb.n > 0 && isblank_((unsigned char)lb.s[lb.n - 1]))
                lb.n--;
            lb.s[lb.n] = 0;
            nl = memchr(p, '\n', (size_t)(end - p));
            le = nl ? nl : end;
            lineno++;
            {
                const char *q = p;
                while (q < le && isblank_((unsigned char)*q))
                    q++;
                sb_addc(&lb, ' ');
                sb_addn(&lb, q, (size_t)(le - q));
            }
            p = nl ? nl + 1 : end;
        }
        {
            char *line = sb_str(&lb), *t;
            const char *r;
            strip_comment(line);
            t = line;
            while (isspace_((unsigned char)*t))
                t++;
            if (*t == 0) {
                free(lb.s);
                continue;
            }
            loc_line = start_line;
            ps.line = start_line;
            if (conditional(&ps, t)) {
                free(lb.s);
                continue;
            }
            if (skipping(&ps)) {
                /* 読み飛ばす枝でも define の入れ子は数える */
                if (starts_kw(t, "define") || strcmp(t, "define") == 0) {
                    in_define = 1;
                    define_depth = 0;
                    def_name = xstrdup("");
                }
                free(lb.s);
                continue;
            }
            {
                /* define (頭に override / export が付いてもよい) */
                const char *q = t;
                int ov = 0, ex = 0;
                for (;;) {
                    if ((r = starts_kw(q, "override")) != 0 && (starts_kw(r, "define") || starts_kw(r, "export"))) {
                        ov = 1;
                        q = r;
                    } else if ((r = starts_kw(q, "export")) != 0 && starts_kw(r, "define")) {
                        ex = EX_EXPORT;
                        q = r;
                    } else
                        break;
                }
                if ((r = starts_kw(q, "define")) != 0) {
                    char *nm = trim(r, strlen(r)), *e;
                    size_t l = strlen(nm);
                    def_op = '=';
                    if (l >= 2 && nm[l - 1] == '=' && strchr(":+?!", nm[l - 2])) {
                        def_op = nm[l - 2] == '!' ? '=' : nm[l - 2];
                        nm[l - 2] = 0;
                        if (l >= 3 && nm[l - 3] == ':')
                            nm[l - 3] = 0;
                    } else if (l >= 1 && nm[l - 1] == '=')
                        nm[l - 1] = 0;
                    e = expand(nm);
                    def_name = trim(e, strlen(e));
                    free(e);
                    free(nm);
                    def_override = ov;
                    def_exp = ex;
                    in_define = 1;
                    define_depth = 0;
                    memset(&def_body, 0, sizeof def_body);
                    end_rule(&ps);
                    free(lb.s);
                    continue;
                }
            }
            if ((r = starts_kw(t, "endef")) != 0)
                fatal_at("extraneous 'endef'%s", "");
            if ((r = starts_kw(t, "include")) != 0 || (r = starts_kw(t, "-include")) != 0
                || (r = starts_kw(t, "sinclude")) != 0) {
                int opt = t[0] != 'i';
                end_rule(&ps);
                do_include(r, opt);
                loc_file = fname;
                free(lb.s);
                continue;
            }
            if ((r = starts_kw(t, "vpath")) != 0) {
                free(lb.s);
                continue;
            }
            if ((r = starts_kw(t, "undefine")) != 0) {
                char *e = expand(r), *nm = trim(e, strlen(e));
                struct var *v = vs_get(&globals, nm);
                if (v) {
                    /* 無い状態に戻す: 表から外す代わりに出所を落とす */
                    free(v->value);
                    v->value = xstrdup("");
                }
                free(e);
                free(nm);
                free(lb.s);
                continue;
            }
            {
                size_t pos, vend;
                int op, kind;
                const char *q = t;
                int exp_only = 0;
                /* export / unexport だけの行 */
                if ((r = starts_kw(t, "export")) != 0 || (r = starts_kw(t, "unexport")) != 0) {
                    q = r;
                    exp_only = 1;
                }
                kind = find_op(q, &pos, &op, &vend);
                if (kind == FIND_ASSIGN) {
                    end_rule(&ps);
                    do_assign(&ps, t, 0, 0, 0);
                } else if (exp_only && kind == FIND_NONE) {
                    end_rule(&ps);
                    do_export(q, t[0] == 'u' ? EX_UNEXPORT : EX_EXPORT);
                } else if (kind == FIND_RULE && !exp_only)
                    do_rule(&ps, t, pos);
                else if (starts_kw(t, "override") && kind == FIND_ASSIGN)
                    do_assign(&ps, t, 0, 0, 0);
                else {
                    /* 関数だけの行 ($(eval ...) や $(info ...)) は展開して空でなければ誤り */
                    char *e = expand(t), *tt = trim(e, strlen(e));
                    if (*tt) {
                        size_t pos2, vend2;
                        int op2;
                        int k2 = find_op(tt, &pos2, &op2, &vend2);
                        if (k2 == FIND_RULE)
                            do_rule(&ps, tt, pos2);
                        else if (k2 == FIND_ASSIGN)
                            do_assign(&ps, tt, 0, 0, 0);
                        else
                            fatal_at("missing separator%s", "");
                    }
                    free(e);
                    free(tt);
                }
            }
        }
        free(lb.s);
    }
    if (in_define)
        fatal_at("missing 'endef', unterminated 'define'%s", "");
    if (ps.ncond > 0)
        fatal_at("missing 'endif'%s", "");
    end_rule(&ps);
    loc_file = save_file;
    loc_line = save_line;
}

static void eval_text(const char *text, const char *fname, int line)
{
    parse_buffer(text, strlen(text), fname ? fname : "<eval>", line > 0 ? line : 1);
}

static void read_makefile(const char *name, int optional)
{
    char *path = 0, *buf;
    size_t len;
    int i;
    struct makefile *m;
    if (nmfiles == cmfiles) {
        cmfiles = cmfiles ? cmfiles * 2 : 16;
        mfiles = xrealloc(mfiles, (size_t)cmfiles * sizeof *mfiles);
    }
    m = &mfiles[nmfiles++];
    m->name = xstrdup(name);
    m->optional = optional;
    m->path = 0;
    if (path_exists(name))
        path = xstrdup(name);
    else if (name[0] != '/') {
        for (i = 0; i < incdirs.n; i++) {
            struct sb b;
            memset(&b, 0, sizeof b);
            sb_adds(&b, incdirs.v[i]);
            sb_addc(&b, '/');
            sb_adds(&b, name);
            if (path_exists(sb_str(&b))) {
                path = sb_take(&b);
                break;
            }
            free(b.s);
        }
    }
    if (!path)
        return;
    buf = read_file(path, &len);
    if (!buf)
        return;
    m = &mfiles[nmfiles - 1];
    m->path = path;
    {
        struct var *v = vs_get(&globals, "MAKEFILE_LIST");
        struct sb b;
        memset(&b, 0, sizeof b);
        if (v)
            sb_adds(&b, v->value);
        sb_addc(&b, ' ');
        sb_adds(&b, path);
        set_simple("MAKEFILE_LIST", sb_str(&b), OR_FILE);
        free(b.s);
    }
    parse_buffer(buf, len, path, 1);
    free(buf);
}

/* ---- 選択肢 ---- */

static int opt_n, opt_s, opt_k, opt_B, opt_r, opt_R, opt_w, opt_nopd;
static struct words cmdvars;    /* 命令行の代入 (MAKEFLAGS で子へ渡す) */

/* ---- 子の環境 ---- */

static int valid_env_name(const char *s)
{
    if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
        return 0;
    for (s++; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    return 1;
}

static int var_exported(struct var *v)
{
    int m = export_mark(v->name);
    if (m == EX_UNEXPORT)
        return 0;
    if (m == EX_EXPORT || export_all)
        return v->origin != OR_AUTO;
    return v->origin == OR_ENV || v->origin == OR_CMDLINE;
}

/* 子へ渡す環境 ("名前=値" の並び，0 で終わる) */
static char **build_env(void)
{
    struct htab seen;
    struct words out;
    struct vset *s;
    char **env;
    int i;
    memset(&seen, 0, sizeof seen);
    memset(&out, 0, sizeof out);
    for (s = ctx; s; s = s->up) {
        unsigned b;
        for (b = 0; b < s->t.nb; b++) {
            struct hent *e;
            for (e = s->t.b[b]; e; e = e->next) {
                struct var *v = e->val;
                struct sb kv;
                if (ht_get(&seen, v->name))
                    continue;
                ht_put(&seen, v->name, v);
                if (!valid_env_name(v->name) || !var_exported(v) || v->expanding)
                    continue;
                if (strcmp(v->name, "MAKEFLAGS") == 0 || strcmp(v->name, "MAKELEVEL") == 0)
                    continue;
                memset(&kv, 0, sizeof kv);
                sb_adds(&kv, v->name);
                sb_addc(&kv, '=');
                env_building++;
                env_cycle = 0;
                var_value_into(&kv, v);
                env_building--;
                if (!env_cycle)
                    words_add(&out, kv.s, kv.n);
                env_cycle = 0;
                free(kv.s);
            }
        }
    }
    {
        struct var *mf = vs_get(&globals, "MAKEFLAGS");
        char lv[32];
        struct sb kv;
        memset(&kv, 0, sizeof kv);
        sb_adds(&kv, "MAKEFLAGS=");
        if (mf)
            sb_adds(&kv, mf->value);
        words_add(&out, kv.s, kv.n);
        free(kv.s);
        sprintf(lv, "MAKELEVEL=%d", makelevel + 1);
        words_add(&out, lv, strlen(lv));
    }
    env = xmalloc((size_t)(out.n + 1) * sizeof(char *));
    for (i = 0; i < out.n; i++)
        env[i] = out.v[i];
    env[out.n] = 0;
    free(out.v);
    {
        unsigned b;
        for (b = 0; b < seen.nb; b++) {
            struct hent *e = seen.b[b], *nx;
            for (; e; e = nx) {
                nx = e->next;
                free(e->key);
                free(e);
            }
        }
        free(seen.b);
    }
    return env;
}

static void free_env(char **env)
{
    int i;
    for (i = 0; env[i]; i++)
        free(env[i]);
    free(env);
}

/* ---- 命令を走らせる ---- */

static char *self_path(const char *argv0);

static char *shell_path(void)
{
    struct var *v = vs_get(&globals, "SHELL");
    char *s = v ? expand(v->value) : xstrdup("/bin/sh"), *t = trim(s, strlen(s));
    free(s);
#ifdef MK_HOST
    /* / を含まない名前は PATH から探す */
    if (!strchr(t, '/')) {
        char *p = self_path(t);
        free(t);
        t = p;
    }
#endif
    return t;
}

#ifdef MK_HOST
/* cmd を $(SHELL) -c で走らせて終わりを待つ。out_fd が 0 以上なら標準出力を
 * そこへ結ぶ。終了コードを返す */
static int run_cmd(const char *cmd, int out_fd)
{
    char **env = build_env();
    char *sh = shell_path();
    pid_t pid;
    int st;
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0)
        die("*** fork できない", 0, 0);
    if (pid == 0) {
        char *argv[4];
        if (out_fd >= 0) {
            dup2(out_fd, 1);
            close(out_fd);
        }
        argv[0] = sh;
        argv[1] = "-c";
        argv[2] = (char *)cmd;
        argv[3] = 0;
        execve(sh, argv, env);
        fprintf(stderr, "%s: %s: %s\n", progname, sh, strerror(errno));
        _exit(127);
    }
    free_env(env);
    free(sh);
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            die("*** waitpid が失敗した", 0, 0);
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

static char *shell_output(const char *cmd, int *status)
{
    int fds[2];
    struct sb b;
    char buf[4096];
    int r;
    char **env;
    char *sh;
    pid_t pid;
    int st;
    memset(&b, 0, sizeof b);
    if (pipe(fds) < 0)
        die("*** pipe が作れない", 0, 0);
    env = build_env();
    sh = shell_path();
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid == 0) {
        char *argv[4];
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        argv[0] = sh;
        argv[1] = "-c";
        argv[2] = (char *)cmd;
        argv[3] = 0;
        execve(sh, argv, env);
        _exit(127);
    }
    close(fds[1]);
    free_env(env);
    free(sh);
    while ((r = (int)read(fds[0], buf, sizeof buf)) > 0)
        sb_addn(&b, buf, (size_t)r);
    close(fds[0]);
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            break;
    *status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    return b.s ? sb_take(&b) : xstrdup("");
}
#else
/* stone: sh2 は -c を持たないので命令を一時ファイルに書いて渡す。環境は
 * libc27 / kernel31 が入るまで渡らない */
#define MK_TMP "_mk.sh"
#define MK_OUT "_mk.out"

static int write_tmp(const char *cmd)
{
    int fd = open(MK_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;
    write(fd, (char *)cmd, strlen(cmd));
    write(fd, "\n", 1);
    close(fd);
    return 0;
}

static int run_cmd(const char *cmd, int out_fd)
{
    char *av[3];
    int r;
    (void)out_fd;
    if (write_tmp(cmd) < 0)
        die("*** 一時ファイルが作れない: %s", MK_TMP, 0);
    fflush(stdout);
    av[0] = "/bin/sh2";
    av[1] = MK_TMP;
    av[2] = 0;
    r = spawn2("/bin/sh2", av, 0, 0, 0);
    unlink(MK_TMP);
    return r < 0 ? 127 : r;
}

static char *shell_output(const char *cmd, int *status)
{
    char *av[3], *r;
    size_t len;
    if (write_tmp(cmd) < 0)
        die("*** 一時ファイルが作れない: %s", MK_TMP, 0);
    fflush(stdout);
    av[0] = "/bin/sh2";
    av[1] = MK_TMP;
    av[2] = 0;
    *status = spawn2("/bin/sh2", av, 0, MK_OUT, 0);
    unlink(MK_TMP);
    r = read_file(MK_OUT, &len);
    unlink(MK_OUT);
    return r ? r : xstrdup("");
}
#endif

/* ---- VPATH ---- */

/* name が無ければ VPATH の階層で探す。見つけた経路を返す (確保する) か 0 */
static char *vpath_find(const char *name)
{
    struct var *v;
    char *val;
    const char *p;
    char *found = 0;
    if (name[0] == '/')
        return 0;
    v = vs_get(&globals, "VPATH");
    if (!v)
        return 0;
    val = v->flavor == FL_SIMPLE ? xstrdup(v->value) : expand(v->value);
    p = val;
    while (*p && !found) {
        const char *e;
        while (*p == ':' || isspace_((unsigned char)*p))
            p++;
        if (!*p)
            break;
        e = p;
        while (*e && *e != ':' && !isspace_((unsigned char)*e))
            e++;
        {
            struct sb b;
            memset(&b, 0, sizeof b);
            sb_addn(&b, p, (size_t)(e - p));
            sb_addc(&b, '/');
            sb_adds(&b, name);
            if (path_exists(sb_str(&b)))
                found = sb_take(&b);
            else
                free(b.s);
        }
        p = e;
    }
    free(val);
    return found;
}

/* 在るか (VPATH を含む) */
static int exists_anywhere(const char *name)
{
    char *v;
    if (path_exists(name))
        return 1;
    v = vpath_find(name);
    if (v) {
        free(v);
        return 1;
    }
    return 0;
}

/* ---- 型規則の探索 ---- */

static int update(struct file *f, struct vset *pctx);

struct match {
    struct prule *r;
    int ti;                     /* 合った目標の型 */
    char *stem;
    struct words deps, oo;
};

static void match_free(struct match *m)
{
    free(m->stem);
    words_free(&m->deps);
    words_free(&m->oo);
    memset(m, 0, sizeof *m);
}

/* 型規則 r の目標の型 ti が name に合うか。合えば幹 (階層の付け直しを含む) */
static int prule_target_match(struct prule *r, int ti, const char *name, char **stem, char **dirpfx)
{
    const char *pat = r->targets.v[ti];
    *dirpfx = 0;
    if (strchr(pat, '/') == 0) {
        const char *sl = strrchr(name, '/');
        if (sl) {
            if (!pat_match(pat, sl + 1, stem))
                return 0;
            *dirpfx = xstrndup(name, (size_t)(sl + 1 - name));
            return 1;
        }
    }
    return pat_match(pat, name, stem);
}

/* 依存の文 text の各語の % を幹に替え (階層を前に付け)，二次展開する */
static void prule_deps(struct match *m, const char *text, const char *dirpfx, int second, struct words *out, const char *target)
{
    struct sb b;
    const char *p = text, *w;
    size_t n;
    int first = 1;
    memset(&b, 0, sizeof b);
    while ((w = next_word(&p, &n)) != 0) {
        char *word = xstrndup(w, n);
        if (!first)
            sb_addc(&b, ' ');
        first = 0;
        if (strchr(word, '%')) {
            if (dirpfx)
                sb_adds(&b, dirpfx);
            /* 型の頭の ./ は除く (GNU make と同じ) */
            pat_subst(&b, norm_name(word), m->stem);
        } else
            sb_adds(&b, word);
        free(word);
    }
    if (second && b.s && strchr(b.s, '$')) {
        struct vset *save = ctx, *a = vs_new(ctx);
        char *e;
        struct sb full;
        vs_set(a, "@", target, FL_SIMPLE, OR_AUTO);
        memset(&full, 0, sizeof full);
        if (dirpfx)
            sb_adds(&full, dirpfx);
        sb_adds(&full, m->stem);
        vs_set(a, "*", sb_str(&full), FL_SIMPLE, OR_AUTO);
        free(full.s);
        ctx = a;
        e = expand(b.s);
        ctx = save;
        vs_free(a);
        words_split(out, e);
        free(e);
    } else if (b.s)
        words_split(out, b.s);
    free(b.s);
}

static int find_implicit(struct file *f, struct match *best, int depth);

/* 依存が在るか，在るべきもの (記述に現れる) か，作れるものか */
static int dep_ok(const char *name, int depth)
{
    struct file *g = file_get(name);
    if (g && (g->is_target || g->cmds))
        return 1;
    if (exists_anywhere(name))
        return 1;
    if (g)
        return 1;
    if (depth < 3) {
        struct file tmp;
        struct match m;
        int ok;
        memset(&tmp, 0, sizeof tmp);
        tmp.name = (char *)name;
        memset(&m, 0, sizeof m);
        ok = find_implicit(&tmp, &m, depth + 1);
        match_free(&m);
        return ok;
    }
    return 0;
}

/* f に合う型規則を探す。幹の短いものを選び，同じなら先に書かれたもの */
static int find_implicit(struct file *f, struct match *best, int depth)
{
    struct prule *r;
    int found = 0;
    for (r = prules; r; r = r->next) {
        int ti;
        if (!r->cmds)
            continue;
        for (ti = 0; ti < r->targets.n; ti++) {
            char *stem, *dirpfx;
            struct match m;
            int ok = 1, i;
            if (!prule_target_match(r, ti, f->name, &stem, &dirpfx))
                continue;
            memset(&m, 0, sizeof m);
            m.r = r;
            m.ti = ti;
            m.stem = stem;
            prule_deps(&m, r->deps, dirpfx, r->second, &m.deps, f->name);
            prule_deps(&m, r->oo, dirpfx, r->second, &m.oo, f->name);
            for (i = 0; i < m.deps.n && ok; i++)
                ok = dep_ok(m.deps.v[i], depth);
            if (ok && dirpfx) {
                /* $* は階層を含む */
                struct sb b;
                memset(&b, 0, sizeof b);
                sb_adds(&b, dirpfx);
                sb_adds(&b, m.stem);
                free(m.stem);
                m.stem = sb_take(&b);
            }
            free(dirpfx);
            if (ok && (!found || strlen(m.stem) < strlen(best->stem))) {
                if (found)
                    match_free(best);
                *best = m;
                found = 1;
            } else
                match_free(&m);
            break;
        }
    }
    return found;
}

/* ---- 目標特有の変数の文脈 ---- */

static void apply_tvar(struct vset *s, struct tvar *t)
{
    struct var *cur;
    struct vset *save = ctx;
    ctx = s;
    cur = var_lookup(t->name);
    if (cur && cur->origin >= OR_CMDLINE && cur->origin != OR_AUTO && !t->override) {
        ctx = save;
        return;
    }
    if (t->op == '+' && cur && vs_get(s, t->name) == 0) {
        /* 外側の値に足す */
        struct sb b;
        memset(&b, 0, sizeof b);
        sb_adds(&b, cur->value);
        if (cur->flavor == FL_SIMPLE) {
            char *e = expand(t->value);
            if (b.n && *e)
                sb_addc(&b, ' ');
            sb_adds(&b, e);
            free(e);
            vs_set(s, t->name, sb_str(&b), FL_SIMPLE, t->override ? OR_OVERRIDE : OR_FILE);
        } else {
            if (b.n && *t->value)
                sb_addc(&b, ' ');
            sb_adds(&b, t->value);
            vs_set(s, t->name, sb_str(&b), FL_RECURSIVE, t->override ? OR_OVERRIDE : OR_FILE);
        }
        free(b.s);
    } else
        assign(s, t->name, t->op, t->value, t->override ? OR_OVERRIDE : OR_FILE, 0);
    if (t->exp)
        mark_export(t->name, t->exp);
    ctx = save;
}

static struct vset *target_ctx(struct file *f, struct vset *pctx)
{
    struct vset *s = vs_new(pctx);
    struct tvar *t;
    for (t = pvars; t; t = t->next)
        if (pat_match(t->pattern, f->name, 0))
            apply_tvar(s, t);
    for (t = f->tvars; t; t = t->next)
        apply_tvar(s, t);
    return s;
}

/* ---- 作る ---- */

static int any_ran;             /* この走りで命令を 1 つでも走らせた */

static void stat_file(struct file *f)
{
    int e;
    f->mtime = file_mtime(f->name, &e);
    f->exists = e;
    free(f->vpath);
    f->vpath = 0;
    if (!e) {
        char *v = vpath_find(f->name);
        if (v) {
            f->vpath = v;
            f->mtime = file_mtime(v, &e);
            f->exists = e;
        }
    }
}

static const char *dep_path(struct dep *d)
{
    return d->f->vpath ? d->f->vpath : d->name;
}

/* 命令行の頭の @ - + と空白を読む */
static const char *cmd_prefix(const char *s, int *silent, int *ignore, int *force)
{
    for (;;) {
        if (*s == '@')
            *silent = 1;
        else if (*s == '-')
            *ignore = 1;
        else if (*s == '+')
            *force = 1;
        else if (!isblank_((unsigned char)*s))
            break;
        s++;
    }
    return s;
}

static int has_make_ref(const char *s)
{
    return strstr(s, "$(MAKE)") || strstr(s, "${MAKE}");
}

/* 目標の命令を走らせる。成功で 0 */
static int run_recipe(struct file *f, struct cmds *c, const char *stem)
{
    struct vset *save = ctx, *a = vs_new(ctx);
    struct sb all, plus, newer, oo;
    struct dep *d;
    struct words lines, recur;
    int *lnums = 0;
    const char *first = 0;
    int i, rc = 0, status = 0;
    mtime_t before = f->mtime;
    int existed = f->exists;

    memset(&all, 0, sizeof all);
    memset(&plus, 0, sizeof plus);
    memset(&newer, 0, sizeof newer);
    memset(&oo, 0, sizeof oo);
    {
        struct htab seen;
        memset(&seen, 0, sizeof seen);
        for (d = f->deps; d; d = d->next) {
            const char *p = dep_path(d);
            if (d->oo) {
                if (oo.n)
                    sb_addc(&oo, ' ');
                sb_adds(&oo, p);
                continue;
            }
            if (!first)
                first = p;
            if (plus.n)
                sb_addc(&plus, ' ');
            sb_adds(&plus, p);
            if (!ht_get(&seen, p)) {
                ht_put(&seen, p, d);
                if (all.n)
                    sb_addc(&all, ' ');
                sb_adds(&all, p);
                if (!f->exists || !d->f->exists || d->f->mtime > f->mtime) {
                    if (newer.n)
                        sb_addc(&newer, ' ');
                    sb_adds(&newer, p);
                }
            }
        }
        {
            unsigned b;
            for (b = 0; b < seen.nb; b++) {
                struct hent *e = seen.b[b], *nx;
                for (; e; e = nx) {
                    nx = e->next;
                    free(e->key);
                    free(e);
                }
            }
            free(seen.b);
        }
    }
    vs_set(a, "@", f->name, FL_SIMPLE, OR_AUTO);
    vs_set(a, "<", first ? first : "", FL_SIMPLE, OR_AUTO);
    vs_set(a, "^", sb_str(&all), FL_SIMPLE, OR_AUTO);
    vs_set(a, "+", sb_str(&plus), FL_SIMPLE, OR_AUTO);
    vs_set(a, "?", sb_str(&newer), FL_SIMPLE, OR_AUTO);
    vs_set(a, "|", sb_str(&oo), FL_SIMPLE, OR_AUTO);
    vs_set(a, "*", stem ? stem : "", FL_SIMPLE, OR_AUTO);
    free(all.s);
    free(plus.s);
    free(newer.s);
    free(oo.s);
    ctx = a;

    /* 全行を展開してから走らせる。展開で改行を含んだ行は改行ごとに分ける */
    memset(&lines, 0, sizeof lines);
    memset(&recur, 0, sizeof recur);
    for (i = 0; i < c->n; i++) {
        char *e;
        size_t k, st = 0, n;
        int rec = has_make_ref(c->lines[i]);
        loc_file = c->file;
        loc_line = c->lnum[i];
        e = expand(c->lines[i]);
        n = strlen(e);
        for (k = 0; k <= n; k++) {
            if (k == n || (e[k] == '\n' && !(k > 0 && e[k - 1] == '\\'))) {
                words_add(&lines, e + st, k - st);
                words_add(&recur, rec ? "1" : "0", 1);
                lnums = xrealloc(lnums, (size_t)lines.n * sizeof(int));
                lnums[lines.n - 1] = c->lnum[i];
                st = k + 1;
            }
        }
        free(e);
    }
    for (i = 0; i < lines.n; i++) {
        int sil = 0, ign = 0, frc = 0;
        const char *cmd = cmd_prefix(lines.v[i], &sil, &ign, &frc);
        int rec = recur.v[i][0] == '1';
        if (!*cmd)
            continue;
        if (!sil || opt_n) {
            if (!opt_s || opt_n) {
                printf("%s\n", cmd);
                fflush(stdout);
            }
        }
        if (opt_n && !frc && !rec)
            continue;
        any_ran = 1;
        status = run_cmd(cmd, -1);
        if (status != 0) {
            fflush(stdout);
            if (ign) {
                fprintf(stderr, "%s: [%s:%d: %s] Error %d (ignored)\n", progname, c->file, lnums[i], f->name, status);
                continue;
            }
            if (makelevel > 0)
                fprintf(stderr, "%s[%d]: *** [%s:%d: %s] Error %d\n", progname, makelevel, c->file, lnums[i], f->name, status);
            else
                fprintf(stderr, "%s: *** [%s:%d: %s] Error %d\n", progname, c->file, lnums[i], f->name, status);
            rc = 1;
            break;
        }
    }
    words_free(&lines);
    words_free(&recur);
    free(lnums);
    ctx = save;
    vs_free(a);
    if (rc && delete_on_error && !f->phony && !f->precious) {
        int e;
        mtime_t m = file_mtime(f->name, &e);
        if (e && (!existed || m != before)) {
            fflush(stdout);
            fprintf(stderr, "%s: *** Deleting file '%s'\n", progname, f->name);
            unlink(f->name);
        }
    }
    return rc;
}

/* f を作る (古ければ)。成功で 0 */
static int update(struct file *f, struct vset *pctx)
{
    struct vset *tctx;
    struct dep *d;
    struct cmds *c;
    struct match m;
    int have_match = 0, rc = 0, remake = 0, i;
    const char *stem;

    if (f->state == ST_DONE)
        return f->status;
    if (f->state == ST_RUNNING) {
        fflush(stdout);
        fprintf(stderr, "%s: Circular %s dependency dropped.\n", progname, f->name);
        return 0;
    }
    f->state = ST_RUNNING;
    stat_file(f);
    if (f->phony)
        f->exists = 0;
    tctx = target_ctx(f, pctx);

    /* 二次展開する依存 */
    if (f->deps2.n) {
        struct vset *save = ctx, *a = vs_new(tctx);
        vs_set(a, "@", f->name, FL_SIMPLE, OR_AUTO);
        vs_set(a, "*", f->stem ? f->stem : "", FL_SIMPLE, OR_AUTO);
        ctx = a;
        for (i = 0; i < f->deps2.n; i++) {
            char *e = expand(f->deps2.v[i]);
            struct words dw, ow;
            int j;
            memset(&dw, 0, sizeof dw);
            memset(&ow, 0, sizeof ow);
            split_deps(e, &dw, &ow);
            for (j = 0; j < dw.n; j++)
                add_dep(f, dw.v[j], 0);
            for (j = 0; j < ow.n; j++)
                add_dep(f, ow.v[j], 1);
            words_free(&dw);
            words_free(&ow);
            free(e);
        }
        ctx = save;
        vs_free(a);
        words_free(&f->deps2);
    }

    c = f->cmds;
    stem = f->stem;
    memset(&m, 0, sizeof m);
    if (!c && !f->phony) {
        if (find_implicit(f, &m, 0)) {
            struct dep *old = f->deps;
            have_match = 1;
            c = m.r->cmds;
            stem = m.stem;
            f->irule = m.r;
            /* 型規則の依存を前に置く ($< がそれになる) */
            f->deps = 0;
            f->dtail = &f->deps;
            for (i = 0; i < m.deps.n; i++)
                add_dep(f, m.deps.v[i], 0);
            for (i = 0; i < m.oo.n; i++)
                add_dep(f, m.oo.v[i], 1);
            for (d = old; d; d = d->next)
                add_dep(f, d->name, d->oo);
        }
    }

    for (d = f->deps; d; d = d->next) {
        int r = update(d->f, tctx);
        if (r == 2) {
            fflush(stdout);
            fprintf(stderr, "%s: *** No rule to make target '%s', needed by '%s'.  Stop.\n", progname, d->name, f->name);
            r = 1;
        }
        if (r) {
            rc = 1;
            if (!opt_k)
                break;
        }
    }
    if (rc) {
        f->state = ST_DONE;
        f->status = 1;
        vs_free(tctx);
        if (have_match)
            match_free(&m);
        return 1;
    }

    if (!c && !f->exists && !f->is_target && !f->deps && !f->phony) {
        f->state = ST_DONE;
        f->status = 1;
        vs_free(tctx);
        return 2;               /* 規則が無い (呼び手が報せる) */
    }

    remake = opt_B || !f->exists || f->phony;
    for (d = f->deps; d && !remake; d = d->next) {
        if (d->oo)
            continue;
        if (!d->f->exists || d->f->mtime > f->mtime)
            remake = 1;
    }
    if (remake && c) {
        struct vset *save = ctx;
        ctx = tctx;
        free(f->vpath);
        f->vpath = 0;
        rc = run_recipe(f, c, stem);
        ctx = save;
        f->ran = 1;
    }
    /* 作り直した目標は時刻を取り直す。VPATH の経路は使わない */
    if (remake) {
        int e;
        f->mtime = file_mtime(f->name, &e);
        f->exists = e && !f->phony;
    }
    /* 複数の目標の型規則は 1 回で全部を作る */
    if (have_match && remake && c && m.r->targets.n > 1) {
        int ti;
        for (ti = 0; ti < m.r->targets.n; ti++) {
            struct sb b;
            struct file *g;
            const char *pat = m.r->targets.v[ti];
            memset(&b, 0, sizeof b);
            if (strchr(pat, '/') == 0 && strrchr(m.stem, '/')) {
                const char *sl = strrchr(m.stem, '/');
                sb_addn(&b, m.stem, (size_t)(sl + 1 - m.stem));
                pat_subst(&b, pat, sl + 1);
            } else
                pat_subst(&b, pat, m.stem);
            g = file_enter(sb_str(&b));
            if (g != f && g->state != ST_DONE) {
                int e;
                g->mtime = file_mtime(g->name, &e);
                g->exists = e;
                g->ran = 1;
                g->state = ST_DONE;
                g->status = rc;
            }
            free(b.s);
        }
    }
    if (have_match)
        match_free(&m);
    vs_free(tctx);
    f->state = ST_DONE;
    f->status = rc;
    return rc;
}

/* 目標を作り，規則が無ければ報せる */
static int update_goal(struct file *f, struct file *parent)
{
    int r = update(f, &globals);
    if (r == 2) {
        fflush(stdout);
        if (parent)
            fprintf(stderr, "%s: *** No rule to make target '%s', needed by '%s'.  Stop.\n", progname, f->name, parent->name);
        else
            fprintf(stderr, "%s: *** No rule to make target '%s'.  Stop.\n", progname, f->name);
        return 1;
    }
    return r;
}

/* ---- MAKEFLAGS ---- */

/* 命令行の代入を MAKEFLAGS に書く形にする (空白と \ を \ で守る) */
static void quote_flag(struct sb *b, const char *s)
{
    for (; *s; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\\')
            sb_addc(b, '\\');
        sb_addc(b, *s);
    }
}

/* 今の選択肢から MAKEFLAGS を組む。1 字の選択肢を頭の語に並べ，長い選択肢，
 * " -- " と命令行の代入を続ける (GNU make 4 の形) */
static void set_makeflags(void)
{
    struct sb b;
    int i;
    memset(&b, 0, sizeof b);
    if (opt_B)
        sb_addc(&b, 'B');
    if (opt_k)
        sb_addc(&b, 'k');
    if (opt_n)
        sb_addc(&b, 'n');
    if (opt_r)
        sb_addc(&b, 'r');
    if (opt_R)
        sb_addc(&b, 'R');
    if (opt_s)
        sb_addc(&b, 's');
    for (i = 0; i < incdirs.n; i++) {
        if (b.n)
            sb_addc(&b, ' ');
        sb_adds(&b, "-I");
        quote_flag(&b, incdirs.v[i]);
    }
    if (opt_nopd) {
        if (b.n)
            sb_addc(&b, ' ');
        sb_adds(&b, "--no-print-directory");
    }
    /* 命令行の代入は後に書かれたものから並べる (GNU make と同じ) */
    if (cmdvars.n) {
        sb_adds(&b, " --");
        for (i = cmdvars.n - 1; i >= 0; i--) {
            sb_addc(&b, ' ');
            quote_flag(&b, cmdvars.v[i]);
        }
    }
    set_simple("MAKEFLAGS", sb_str(&b), OR_FILE);
    mark_export("MAKEFLAGS", EX_EXPORT);
    free(b.s);
}

static void add_incdir(const char *d)
{
    int i;
    for (i = 0; i < incdirs.n; i++)
        if (strcmp(incdirs.v[i], d) == 0)
            return;
    words_add(&incdirs, d, strlen(d));
}

/* 命令行の代入 NAME=value (NAME:=value など) なら立てて 1 */
static int cmdline_assign(const char *a)
{
    size_t pos, vend;
    int op;
    char *name;
    if (find_op(a, &pos, &op, &vend) != FIND_ASSIGN)
        return 0;
    name = trim(a, vend);
    if (!*name || strchr(name, ' ')) {
        free(name);
        return 0;
    }
    assign(&globals, name, op == '!' ? '=' : op, a + pos, OR_CMDLINE, 0);
    {
        /* 同じ名前の古い代入を除いてから足す */
        int i, j = 0;
        size_t nl = strlen(name);
        for (i = 0; i < cmdvars.n; i++) {
            if (strncmp(cmdvars.v[i], name, nl) == 0 && strchr(":+?=", cmdvars.v[i][nl]))
                free(cmdvars.v[i]);
            else
                cmdvars.v[j++] = cmdvars.v[i];
        }
        cmdvars.n = j;
        words_add(&cmdvars, a, strlen(a));
    }
    free(name);
    return 1;
}

/* MAKEFLAGS の文 (環境や記述の値) を読む。頭の語が - で始まらなければ
 * 1 字の選択肢の並び */
static void parse_makeflags(const char *s, int with_vars)
{
    struct words w;
    int i, dashdash = 0;
    char *copy;
    /* \ で守った空白を考えて語に分ける */
    memset(&w, 0, sizeof w);
    {
        const char *p = s;
        struct sb cur;
        memset(&cur, 0, sizeof cur);
        for (;;) {
            if (*p == '\\' && p[1]) {
                sb_addc(&cur, p[1]);
                p += 2;
                continue;
            }
            if (*p == 0 || isblank_((unsigned char)*p)) {
                if (cur.n)
                    words_add(&w, cur.s, cur.n);
                cur.n = 0;
                if (cur.s)
                    cur.s[0] = 0;
                if (*p == 0)
                    break;
                p++;
                continue;
            }
            sb_addc(&cur, *p);
            p++;
        }
        free(cur.s);
    }
    for (i = 0; i < w.n; i++) {
        const char *a = w.v[i];
        /* -- の後でも - で始まる語は選択肢として読む (記述が MAKEFLAGS += で
         * 後ろに足したもの) */
        if (dashdash && a[0] != '-') {
            if (with_vars)
                cmdline_assign(a);
            continue;
        }
        if (strcmp(a, "--") == 0) {
            dashdash = 1;
            continue;
        }
        if (a[0] == '-' && a[1] == '-') {
            if (strcmp(a, "--no-print-directory") == 0)
                opt_nopd = 1;
            else if (strncmp(a, "--include-dir=", 14) == 0)
                add_incdir(a + 14);
            continue;
        }
        if (a[0] == '-' && a[1] == 'I') {
            if (a[2])
                add_incdir(a + 2);
            else if (i + 1 < w.n)
                add_incdir(w.v[++i]);
            continue;
        }
        if (i == 0 || a[0] == '-') {
            copy = xstrdup(a[0] == '-' ? a + 1 : a);
            {
                char *c;
                for (c = copy; *c; c++) {
                    switch (*c) {
                    case 'B': opt_B = 1; break;
                    case 'k': opt_k = 1; break;
                    case 'n': opt_n = 1; break;
                    case 'r': opt_r = 1; break;
                    case 'R': opt_R = 1; break;
                    case 's': opt_s = 1; break;
                    case 'w': opt_w = 1; break;
                    default: break;
                    }
                }
            }
            free(copy);
        }
    }
    words_free(&w);
}

/* ---- 始まり ---- */

static char *self_path(const char *argv0)
{
#ifdef MK_HOST
    if (strchr(argv0, '/'))
        return argv0[0] == '/' ? xstrdup(argv0) : abs_path(argv0);
    {
        const char *path = getenv("PATH");
        const char *p = path ? path : "";
        while (*p) {
            const char *e = strchr(p, ':');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            struct sb b;
            memset(&b, 0, sizeof b);
            sb_addn(&b, p, n);
            sb_addc(&b, '/');
            sb_adds(&b, argv0);
            if (access(sb_str(&b), X_OK) == 0)
                return sb_take(&b);
            free(b.s);
            p += n;
            if (*p == ':')
                p++;
        }
    }
    return xstrdup(argv0);
#else
    return argv0[0] == '/' ? xstrdup(argv0) : abs_path(argv0);
#endif
}

static void dir_message(const char *what)
{
    char *cd = cur_dir();
    if (makelevel > 0)
        printf("%s[%d]: %s directory '%s'\n", progname, makelevel, what, cd);
    else
        printf("%s: %s directory '%s'\n", progname, what, cd);
    fflush(stdout);
    free(cd);
}

static void usage(void)
{
    fprintf(stderr, "usage: mk [-f file] [-C dir] [-I dir] [-n] [-s] [-k] [-r] [-R] [-B] [-j[N]] [--no-print-directory] [var=value...] [target...]\n");
    exit(2);
}

/* 読み込んだ記述を作り直す。作り直したものがあれば 1 */
static int remake_makefiles(void)
{
    int i, remade = 0, n = nmfiles;
    for (i = 0; i < n; i++) {
        struct makefile *mf = &mfiles[i];
        struct file *f = file_get(mf->name);
        struct match m;
        int has_rule;
        mtime_t before;
        int existed;
        if (f && f->state == ST_DONE)
            continue;
        memset(&m, 0, sizeof m);
        has_rule = f && (f->cmds || f->is_target);
        if (!has_rule) {
            struct file tmp;
            memset(&tmp, 0, sizeof tmp);
            tmp.name = mf->name;
            has_rule = find_implicit(&tmp, &m, 0);
            match_free(&m);
        }
        if (!has_rule)
            continue;
        f = file_enter(mf->name);
        if (f->phony)
            continue;
        before = file_mtime(mf->name, &existed);
        {
            int saved_n = opt_n;
            int r = update(f, &globals);
            opt_n = saved_n;
            if (r == 1) {
                if (!mf->optional)
                    exit(2);
                continue;
            }
        }
        if (f->ran) {
            int e;
            mtime_t after = file_mtime(mf->name, &e);
            if (e && (!existed || after != before))
                remade = 1;
        }
    }
    return remade;
}

int main(int argc, char **argv)
{
    struct words mkfiles, goals;
    int i, rc = 0, restarts = 0;
    char *self;
    const char *v;

    setvbuf(stdout, 0, _IOFBF, 0);
    memset(&mkfiles, 0, sizeof mkfiles);
    memset(&goals, 0, sizeof goals);
    v = getenv("MAKELEVEL");
    makelevel = v ? atoi(v) : 0;
    v = getenv("MAKE_RESTARTS");
    restarts = v ? atoi(v) : 0;

#ifdef MK_HOST
    /* 環境を変数に取り込む (SHELL と MAKEFLAGS は除く) */
    {
        char **e;
        for (e = environ; *e; e++) {
            const char *eq = strchr(*e, '=');
            char *name;
            if (!eq)
                continue;
            name = xstrndup(*e, (size_t)(eq - *e));
            if (strcmp(name, "SHELL") != 0 && strcmp(name, "MAKEFLAGS") != 0 && strcmp(name, "MAKELEVEL") != 0
                && strcmp(name, "MAKE_RESTARTS") != 0)
            {
                vs_set(&globals, name, eq + 1, FL_RECURSIVE, OR_ENV);
                /* 環境から来た変数は記述が書き換えても子へ輸出する (GNU make と同じ) */
                if (valid_env_name(name))
                    mark_export(name, EX_EXPORT);
            }
            free(name);
        }
    }
#endif
    /* $(MAKE) は起動した時の場所で解く (-C で移る前) */
    self = self_path(argv[0]);
    v = getenv("MAKEFLAGS");
    if (v)
        parse_makeflags(v, 1);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] && a[1] != '-') {
            const char *c = a + 1;
            for (; *c; c++) {
                switch (*c) {
                case 'B': opt_B = 1; break;
                case 'k': opt_k = 1; break;
                case 'n': opt_n = 1; break;
                case 'r': opt_r = 1; break;
                case 'R': opt_R = 1; break;
                case 's': opt_s = 1; break;
                case 'w': opt_w = 1; break;
                case 'j':
                    /* 逐次に走らせる。数は読み捨てる */
                    while (c[1] >= '0' && c[1] <= '9')
                        c++;
                    break;
                case 'f': case 'C': case 'I': {
                    const char *val = c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : 0);
                    if (!val)
                        usage();
                    if (*c == 'f')
                        words_add(&mkfiles, val, strlen(val));
                    else if (*c == 'C') {
                        if (chdir((char *)val) != 0)
                            die("*** %s: 階層に入れない.  Stop.", val, 0);
                        opt_w = 1;
                    } else
                        add_incdir(val);
                    c = " ";            /* この語の残りは値だった */
                    c += strlen(c) - 1;
                    break;
                }
                default:
                    die("*** 知らない選択肢 -%s", c, 0);
                }
            }
            continue;
        }
        if (strcmp(a, "--no-print-directory") == 0) {
            opt_nopd = 1;
            continue;
        }
        if (strcmp(a, "--print-directory") == 0) {
            opt_w = 1;
            continue;
        }
        if (strncmp(a, "--include-dir=", 14) == 0) {
            add_incdir(a + 14);
            continue;
        }
        if (strncmp(a, "--file=", 7) == 0 || strncmp(a, "--makefile=", 11) == 0) {
            const char *val = strchr(a, '=') + 1;
            words_add(&mkfiles, val, strlen(val));
            continue;
        }
        if (strncmp(a, "--directory=", 12) == 0) {
            if (chdir((char *)(a + 12)) != 0)
                die("*** %s: 階層に入れない.  Stop.", a + 12, 0);
            opt_w = 1;
            continue;
        }
        if (strcmp(a, "--") == 0)
            continue;
        if (a[0] == '-' && a[1] == '-')
            die("*** 知らない選択肢 %s", a, 0);
        if (cmdline_assign(a))
            continue;
        words_add(&goals, a, strlen(a));
    }

    set_simple("MAKE", self, OR_DEFAULT);
    set_simple("MAKE_VERSION", MK_VERSION, OR_DEFAULT);
    {
        char *cd = cur_dir(), lv[32];
        set_simple("CURDIR", cd, OR_FILE);
        free(cd);
        sprintf(lv, "%d", makelevel);
        set_simple("MAKELEVEL", lv, OR_ENV);
    }
    {
        struct sb b;
        memset(&b, 0, sizeof b);
        for (i = 0; i < goals.n; i++) {
            if (i)
                sb_addc(&b, ' ');
            sb_adds(&b, goals.v[i]);
        }
        set_simple("MAKECMDGOALS", sb_str(&b), OR_DEFAULT);
        free(b.s);
    }
    if (!vs_get(&globals, "SHELL"))
        set_simple("SHELL", "/bin/sh", OR_DEFAULT);
    set_simple(".SHELLFLAGS", "-c", OR_DEFAULT);
    set_simple("MAKEFILE_LIST", "", OR_FILE);
    set_makeflags();
    {
        /* 命令行の代入 (GNU make の MAKEOVERRIDES と同じ並び) */
        struct sb b;
        memset(&b, 0, sizeof b);
        for (i = cmdvars.n - 1; i >= 0; i--) {
            if (b.n)
                sb_addc(&b, ' ');
            quote_flag(&b, cmdvars.v[i]);
        }
        set_simple("MAKEOVERRIDES", sb_str(&b), OR_DEFAULT);
        free(b.s);
    }

    if ((opt_w || makelevel > 0) && !opt_nopd && !opt_s)
        dir_message("Entering");

    if (mkfiles.n == 0) {
        static const char *defaults[] = { "GNUmakefile", "makefile", "Makefile", 0 };
        for (i = 0; defaults[i]; i++)
            if (path_exists(defaults[i])) {
                words_add(&mkfiles, defaults[i], strlen(defaults[i]));
                break;
            }
    }
    for (i = 0; i < mkfiles.n; i++) {
        int at = nmfiles;
        read_makefile(mkfiles.v[i], 0);
        if (!mfiles[at].path && !file_get(mkfiles.v[i]))
            die("*** %s: No such file or directory.  Stop.", mkfiles.v[i], 0);
    }

    /* 記述が書き足した MAKEFLAGS を読み直し，子へ渡す形に整える */
    {
        struct var *mf = vs_get(&globals, "MAKEFLAGS");
        if (mf) {
            char *val = mf->flavor == FL_SIMPLE ? xstrdup(mf->value) : expand(mf->value);
            parse_makeflags(val, 0);
            free(val);
        }
        set_makeflags();
    }

    loc_file = 0;
    if (remake_makefiles()) {
        if (restarts >= 10)
            die("*** 記述を作り直す回数が多すぎる.  Stop.", 0, 0);
#ifdef MK_HOST
        {
            char nb[32];
            sprintf(nb, "%d", restarts + 1);
            setenv("MAKE_RESTARTS", nb, 1);
            fflush(stdout);
            execv(self, argv);
            die("*** 自分を起動し直せない: %s", self, 0);
        }
#else
        {
            char nb[32];
            sprintf(nb, "%d", restarts + 1);
            (void)nb;
            fflush(stdout);
            exit(spawn2(self, argv, 0, 0, 0));
        }
#endif
    }
    for (i = 0; i < nmfiles; i++)
        if (!mfiles[i].path && !mfiles[i].optional && !path_exists(mfiles[i].name)) {
            fflush(stdout);
            fprintf(stderr, "%s: *** %s: No such file or directory.  Stop.\n", progname, mfiles[i].name);
            exit(2);
        }

    if (goals.n == 0) {
        struct var *dg = vs_get(&globals, ".DEFAULT_GOAL");
        char *g = dg ? expand(dg->value) : (default_goal ? xstrdup(default_goal) : 0);
        if (g && *g)
            words_split(&goals, g);
        free(g);
        if (goals.n == 0)
            die("*** No targets.  Stop.", 0, 0);
    }
    for (i = 0; i < goals.n; i++) {
        struct file *f = file_enter(goals.v[i]);
        int before = any_ran, r;
        any_ran = 0;
        r = update_goal(f, 0);
        if (r) {
            rc = 2;
            if (!opt_k)
                break;
        } else if (!any_ran && makelevel == 0) {
            if (f->cmds || f->irule)
                printf("%s: '%s' is up to date.\n", progname, f->name);
            else
                printf("%s: Nothing to be done for '%s'.\n", progname, f->name);
        }
        any_ran = any_ran || before;
    }
    if ((opt_w || makelevel > 0) && !opt_nopd && !opt_s)
        dir_message("Leaving");
    fflush(stdout);
    return rc;
}
