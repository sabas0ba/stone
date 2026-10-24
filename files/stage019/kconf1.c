/* kconf1.c --- Linux の Kconfig を読み，設定を作る道具 (docs/stage019-linux.md 4 章)
 *
 * Linux 5.4 の scripts/kconfig/conf の代わりに使う。上流の conf は字句解析器を
 * flex で，構文解析器を bison で生成する。我々は外部の生成器を使わないので，
 * 字句解析と構文解析を手で書く (再帰下降)。値の計算と出力は上流の conf と
 * バイト単位で同じになることを目指し，tools/kconfref.sh が組む上流の conf と
 * 突き合わせて確かめる。
 *
 *   kconf1 [-s] --<mode> <Kconfig>
 *     mode: allnoconfig allyesconfig allmodconfig alldefconfig olddefconfig
 *           syncconfig defconfig=<file> savedefconfig=<file>
 *
 * 入出力は上流と同じである。作業ディレクトリが出力先 (O=) で，Kconfig は
 * まず作業ディレクトリから，無ければ環境変数 srctree の下から開く。
 *
 * ---- 計算の順を上流に合わせる ----
 *
 * 記号の値は必要になった時に計算し，記号ごとに覚える (SYMBOL_VALID)。依存が
 * 循環すると，計算を始めた記号によって答が変わる。上流と同じ答を出すため，
 * 計算を始める順 (記号のハッシュ表の順と menu の順) と，覚えた値を捨てる
 * 時点を上流に合わせてある。include/config/auto.conf と autoconf.h は
 * ハッシュ表の順に書かれるので，表の大きさ (9973) とハッシュ関数 (FNV-1 32 bit)，
 * 鎖の先頭へ足す順も合わせる。
 *
 * menu の木 (自動の副 menu) は式の簡約の結果で決まり，.config の
 * "# end of ..." の位置を左右する。そのため式の簡約 (同じ項の除去・
 * 比較への書換え) も上流と同じ規則で行う。
 *
 * ---- 上流と違う点 ----
 *
 *   - 対話の入力 (oldconfig / oldaskconfig / listnewconfig / randconfig) を
 *     持たない。syncconfig で値の無い新しい記号が見つかれば止まる
 *     (先に olddefconfig を回す)
 *   - 依存の循環の検査 (sym_check_deps) と，属性の型の検査 (sym_check_prop)
 *     を持たない。警告の文面は上流と同じとは限らない
 *   - 式の木を解放しない (1 回の実行で終わる道具なので，回収しない)
 *
 * 上流の実装 (GPL-2.0) は仕様を知るために読んだ。符号は写していない。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* ---- 共通 ---- */

static void die(const char *fmt, ...)
{
    va_list ap;
    fputs("kconf1: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p;
    p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static void *xcalloc(size_t n)
{
    void *p;
    p = xmalloc(n);
    memset(p, 0, n);
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p;
    p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

/* 伸びる文字列 */
struct sbuf {
    char *s;
    size_t len;
    size_t cap;
};

static void sb_init(struct sbuf *b)
{
    b->cap = 64;
    b->s = xmalloc(b->cap);
    b->s[0] = 0;
    b->len = 0;
}

static void sb_addn(struct sbuf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap)
            b->cap = b->cap * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len = b->len + n;
    b->s[b->len] = 0;
}

static void sb_add(struct sbuf *b, const char *s)
{
    sb_addn(b, s, strlen(s));
}

/* ---- 型と定数 ---- */

#define no  0
#define mod 1
#define yes 2
#define EXPR_OR(a, b)  ((a) > (b) ? (a) : (b))
#define EXPR_AND(a, b) ((a) < (b) ? (a) : (b))
#define EXPR_NOT(a)    (2 - (a))

#define E_NONE    0
#define E_OR      1
#define E_AND     2
#define E_NOT     3
#define E_EQUAL   4
#define E_UNEQUAL 5
#define E_LTH     6
#define E_LEQ     7
#define E_GTH     8
#define E_GEQ     9
#define E_LIST    10
#define E_SYMBOL  11
#define E_RANGE   12

#define S_UNKNOWN  0
#define S_BOOLEAN  1
#define S_TRISTATE 2
#define S_INT      3
#define S_HEX      4
#define S_STRING   5

#define S_DEF_USER 0
#define S_DEF_AUTO 1
#define S_DEF_COUNT 4

#define SYMBOL_CONST      0x0001
#define SYMBOL_CHOICE     0x0010
#define SYMBOL_CHOICEVAL  0x0020
#define SYMBOL_VALID      0x0080
#define SYMBOL_OPTIONAL   0x0100
#define SYMBOL_WRITE      0x0200
#define SYMBOL_CHANGED    0x0400
#define SYMBOL_WRITTEN    0x0800
#define SYMBOL_NO_WRITE   0x1000
#define SYMBOL_DEF        0x10000
#define SYMBOL_DEF_USER   0x10000
#define SYMBOL_DEF_AUTO   0x20000
#define SYMBOL_NEED_SET_CHOICE_VALUES 0x100000
#define SYMBOL_ALLNOCONFIG_Y 0x200000

#define SYMBOL_HASHSIZE 9973

#define P_UNKNOWN 0
#define P_PROMPT  1
#define P_COMMENT 2
#define P_MENU    3
#define P_DEFAULT 4
#define P_CHOICE  5
#define P_SELECT  6
#define P_IMPLY   7
#define P_RANGE   8
#define P_SYMBOL  9

struct expr;
struct sym;

union edata {
    struct expr *expr;
    struct sym *sym;
};

struct expr {
    int type;
    union edata left;
    union edata right;
};

struct sval {
    void *val;
    int tri;
};

struct evalue {
    struct expr *expr;
    int tri;
};

struct kfile {
    struct kfile *next;
    struct kfile *parent;
    char *name;
    int lineno;
};

struct prop;
struct menu;

struct sym {
    struct sym *next;
    char *name;
    int type;
    struct sval curr;
    struct sval def[S_DEF_COUNT];
    int visible;
    int flags;
    struct prop *prop;
    struct evalue dir_dep;
    struct evalue rev_dep;
    struct evalue implied;
};

struct prop {
    struct prop *next;
    struct sym *sym;
    int type;
    char *text;
    struct evalue visible;
    struct expr *expr;
    struct menu *menu;
    struct kfile *file;
    int lineno;
};

struct menu {
    struct menu *next;
    struct menu *parent;
    struct menu *list;
    struct sym *sym;
    struct prop *prompt;
    struct expr *visibility;
    struct expr *dep;
    int has_help;
    struct kfile *file;
    int lineno;
};

static struct sym symbol_yes;
static struct sym symbol_mod;
static struct sym symbol_no;
static struct sym symbol_empty;
static struct sym *symbol_hash[SYMBOL_HASHSIZE];
static struct sym *sym_defconfig_list;
static struct sym *modules_sym;
static int modules_val;

static struct menu rootmenu;
static struct menu *current_menu;
static struct menu *current_entry;
static struct menu **last_entry_ptr;

static struct kfile *file_list;
static struct kfile *current_file;

static int sym_change_count;
static int silent;

static void sym_calc_value(struct sym *s);
static int expr_calc_value(struct expr *e);

/* ---- 式 ---- */

static int trans_count;

static struct expr *expr_alloc_symbol(struct sym *s)
{
    struct expr *e;
    e = xcalloc(sizeof(*e));
    e->type = E_SYMBOL;
    e->left.sym = s;
    return e;
}

static struct expr *expr_alloc_one(int type, struct expr *ce)
{
    struct expr *e;
    e = xcalloc(sizeof(*e));
    e->type = type;
    e->left.expr = ce;
    return e;
}

static struct expr *expr_alloc_two(int type, struct expr *e1, struct expr *e2)
{
    struct expr *e;
    e = xcalloc(sizeof(*e));
    e->type = type;
    e->left.expr = e1;
    e->right.expr = e2;
    return e;
}

static struct expr *expr_alloc_comp(int type, struct sym *s1, struct sym *s2)
{
    struct expr *e;
    e = xcalloc(sizeof(*e));
    e->type = type;
    e->left.sym = s1;
    e->right.sym = s2;
    return e;
}

static struct expr *expr_alloc_and(struct expr *e1, struct expr *e2)
{
    if (!e1)
        return e2;
    return e2 ? expr_alloc_two(E_AND, e1, e2) : e1;
}

static struct expr *expr_alloc_or(struct expr *e1, struct expr *e2)
{
    if (!e1)
        return e2;
    return e2 ? expr_alloc_two(E_OR, e1, e2) : e1;
}

static struct expr *expr_copy(struct expr *org)
{
    struct expr *e;
    if (!org)
        return 0;
    e = xmalloc(sizeof(*e));
    *e = *org;
    switch (org->type) {
    case E_SYMBOL:
        break;
    case E_NOT:
        e->left.expr = expr_copy(org->left.expr);
        break;
    case E_EQUAL:
    case E_GEQ:
    case E_GTH:
    case E_LEQ:
    case E_LTH:
    case E_UNEQUAL:
        break;
    case E_AND:
    case E_OR:
    case E_LIST:
        e->left.expr = expr_copy(org->left.expr);
        e->right.expr = expr_copy(org->right.expr);
        break;
    default:
        fprintf(stderr, "can't copy type %d\n", e->type);
        e = 0;
        break;
    }
    return e;
}

static int expr_is_yes(struct expr *e)
{
    return !e || (e->type == E_SYMBOL && e->left.sym == &symbol_yes);
}

static int expr_eq(struct expr *e1, struct expr *e2);
static struct expr *expr_eliminate_yn(struct expr *e);

/* 同じ演算子で結ばれた 2 つの木から，両方にある項を取り除く */
static void elim_eq1(int type, struct expr **ep1, struct expr **ep2)
{
    if ((*ep1)->type == type) {
        elim_eq1(type, &(*ep1)->left.expr, ep2);
        elim_eq1(type, &(*ep1)->right.expr, ep2);
        return;
    }
    if ((*ep2)->type == type) {
        elim_eq1(type, ep1, &(*ep2)->left.expr);
        elim_eq1(type, ep1, &(*ep2)->right.expr);
        return;
    }
    if ((*ep1)->type == E_SYMBOL && (*ep2)->type == E_SYMBOL &&
        (*ep1)->left.sym == (*ep2)->left.sym &&
        ((*ep1)->left.sym == &symbol_yes || (*ep1)->left.sym == &symbol_no))
        return;
    if (!expr_eq(*ep1, *ep2))
        return;
    trans_count = trans_count + 1;
    if (type == E_OR) {
        *ep1 = expr_alloc_symbol(&symbol_no);
        *ep2 = expr_alloc_symbol(&symbol_no);
    } else if (type == E_AND) {
        *ep1 = expr_alloc_symbol(&symbol_yes);
        *ep2 = expr_alloc_symbol(&symbol_yes);
    }
}

static void expr_eliminate_eq(struct expr **ep1, struct expr **ep2)
{
    if (!*ep1 || !*ep2)
        return;
    switch ((*ep1)->type) {
    case E_OR:
    case E_AND:
        elim_eq1((*ep1)->type, ep1, ep2);
        break;
    default:
        break;
    }
    if ((*ep1)->type != (*ep2)->type) {
        switch ((*ep2)->type) {
        case E_OR:
        case E_AND:
            elim_eq1((*ep2)->type, ep1, ep2);
            break;
        default:
            break;
        }
    }
    *ep1 = expr_eliminate_yn(*ep1);
    *ep2 = expr_eliminate_yn(*ep2);
}

static int expr_eq(struct expr *e1, struct expr *e2)
{
    int res;
    int old_count;
    if (!e1 || !e2)
        return expr_is_yes(e1) && expr_is_yes(e2);
    if (e1->type != e2->type)
        return 0;
    switch (e1->type) {
    case E_EQUAL:
    case E_GEQ:
    case E_GTH:
    case E_LEQ:
    case E_LTH:
    case E_UNEQUAL:
        return e1->left.sym == e2->left.sym && e1->right.sym == e2->right.sym;
    case E_SYMBOL:
        return e1->left.sym == e2->left.sym;
    case E_NOT:
        return expr_eq(e1->left.expr, e2->left.expr);
    case E_AND:
    case E_OR:
        e1 = expr_copy(e1);
        e2 = expr_copy(e2);
        old_count = trans_count;
        expr_eliminate_eq(&e1, &e2);
        res = (e1->type == E_SYMBOL && e2->type == E_SYMBOL &&
               e1->left.sym == e2->left.sym);
        trans_count = old_count;
        return res;
    default:
        break;
    }
    return 0;
}

/* y と n を含む && / || を畳む */
static struct expr *expr_eliminate_yn(struct expr *e)
{
    if (!e)
        return e;
    switch (e->type) {
    case E_AND:
        e->left.expr = expr_eliminate_yn(e->left.expr);
        e->right.expr = expr_eliminate_yn(e->right.expr);
        if (e->left.expr->type == E_SYMBOL) {
            if (e->left.expr->left.sym == &symbol_no) {
                e->type = E_SYMBOL;
                e->left.sym = &symbol_no;
                e->right.expr = 0;
                return e;
            } else if (e->left.expr->left.sym == &symbol_yes) {
                *e = *(e->right.expr);
                return e;
            }
        }
        if (e->right.expr->type == E_SYMBOL) {
            if (e->right.expr->left.sym == &symbol_no) {
                e->type = E_SYMBOL;
                e->left.sym = &symbol_no;
                e->right.expr = 0;
                return e;
            } else if (e->right.expr->left.sym == &symbol_yes) {
                *e = *(e->left.expr);
                return e;
            }
        }
        break;
    case E_OR:
        e->left.expr = expr_eliminate_yn(e->left.expr);
        e->right.expr = expr_eliminate_yn(e->right.expr);
        if (e->left.expr->type == E_SYMBOL) {
            if (e->left.expr->left.sym == &symbol_no) {
                *e = *(e->right.expr);
                return e;
            } else if (e->left.expr->left.sym == &symbol_yes) {
                e->type = E_SYMBOL;
                e->left.sym = &symbol_yes;
                e->right.expr = 0;
                return e;
            }
        }
        if (e->right.expr->type == E_SYMBOL) {
            if (e->right.expr->left.sym == &symbol_no) {
                *e = *(e->left.expr);
                return e;
            } else if (e->right.expr->left.sym == &symbol_yes) {
                e->type = E_SYMBOL;
                e->left.sym = &symbol_yes;
                e->right.expr = 0;
                return e;
            }
        }
        break;
    default:
        break;
    }
    return e;
}

/* 項の記号を取り出す。比較・記号・その否定だけが対象 */
static int join_syms(struct expr *e1, struct expr *e2, struct sym **s1, struct sym **s2)
{
    struct expr *tmp;
    if (e1->type != E_EQUAL && e1->type != E_UNEQUAL && e1->type != E_SYMBOL && e1->type != E_NOT)
        return 0;
    if (e2->type != E_EQUAL && e2->type != E_UNEQUAL && e2->type != E_SYMBOL && e2->type != E_NOT)
        return 0;
    if (e1->type == E_NOT) {
        tmp = e1->left.expr;
        if (tmp->type != E_EQUAL && tmp->type != E_UNEQUAL && tmp->type != E_SYMBOL)
            return 0;
        *s1 = tmp->left.sym;
    } else
        *s1 = e1->left.sym;
    if (e2->type == E_NOT) {
        if (e2->left.expr->type != E_SYMBOL)
            return 0;
        *s2 = e2->left.expr->left.sym;
    } else
        *s2 = e2->left.sym;
    if (*s1 != *s2)
        return 0;
    if ((*s1)->type != S_BOOLEAN && (*s1)->type != S_TRISTATE)
        return 0;
    return 1;
}

static int is_cmp(struct expr *e, int type, struct sym *v)
{
    return e->type == type && e->right.sym == v;
}

static struct expr *expr_join_or(struct expr *e1, struct expr *e2)
{
    struct sym *sym1;
    struct sym *sym2;
    if (expr_eq(e1, e2))
        return expr_copy(e1);
    if (!join_syms(e1, e2, &sym1, &sym2))
        return 0;
    if (sym1->type == S_TRISTATE) {
        if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
            ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_mod) ||
             (e1->right.sym == &symbol_mod && e2->right.sym == &symbol_yes)))
            return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_no);
        if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
            ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_no) ||
             (e1->right.sym == &symbol_no && e2->right.sym == &symbol_yes)))
            return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_mod);
        if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
            ((e1->right.sym == &symbol_mod && e2->right.sym == &symbol_no) ||
             (e1->right.sym == &symbol_no && e2->right.sym == &symbol_mod)))
            return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_yes);
    }
    if (sym1->type == S_BOOLEAN && sym1 == sym2) {
        if ((e1->type == E_NOT && e1->left.expr->type == E_SYMBOL && e2->type == E_SYMBOL) ||
            (e2->type == E_NOT && e2->left.expr->type == E_SYMBOL && e1->type == E_SYMBOL))
            return expr_alloc_symbol(&symbol_yes);
    }
    return 0;
}

static struct expr *expr_join_and(struct expr *e1, struct expr *e2)
{
    struct sym *sym1;
    struct sym *sym2;
    if (expr_eq(e1, e2))
        return expr_copy(e1);
    if (!join_syms(e1, e2, &sym1, &sym2))
        return 0;
    if ((e1->type == E_SYMBOL && is_cmp(e2, E_EQUAL, &symbol_yes)) ||
        (e2->type == E_SYMBOL && is_cmp(e1, E_EQUAL, &symbol_yes)))
        return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);
    if ((e1->type == E_SYMBOL && is_cmp(e2, E_UNEQUAL, &symbol_no)) ||
        (e2->type == E_SYMBOL && is_cmp(e1, E_UNEQUAL, &symbol_no)))
        return expr_alloc_symbol(sym1);
    if ((e1->type == E_SYMBOL && is_cmp(e2, E_UNEQUAL, &symbol_mod)) ||
        (e2->type == E_SYMBOL && is_cmp(e1, E_UNEQUAL, &symbol_mod)))
        return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);
    if (sym1->type == S_TRISTATE) {
        if (e1->type == E_EQUAL && e2->type == E_UNEQUAL) {
            sym2 = e1->right.sym;
            if ((e2->right.sym->flags & SYMBOL_CONST) && (sym2->flags & SYMBOL_CONST))
                return sym2 != e2->right.sym ? expr_alloc_comp(E_EQUAL, sym1, sym2)
                                             : expr_alloc_symbol(&symbol_no);
        }
        if (e1->type == E_UNEQUAL && e2->type == E_EQUAL) {
            sym2 = e2->right.sym;
            if ((e1->right.sym->flags & SYMBOL_CONST) && (sym2->flags & SYMBOL_CONST))
                return sym2 != e1->right.sym ? expr_alloc_comp(E_EQUAL, sym1, sym2)
                                             : expr_alloc_symbol(&symbol_no);
        }
        if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
            ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_no) ||
             (e1->right.sym == &symbol_no && e2->right.sym == &symbol_yes)))
            return expr_alloc_comp(E_EQUAL, sym1, &symbol_mod);
        if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
            ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_mod) ||
             (e1->right.sym == &symbol_mod && e2->right.sym == &symbol_yes)))
            return expr_alloc_comp(E_EQUAL, sym1, &symbol_no);
        if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
            ((e1->right.sym == &symbol_mod && e2->right.sym == &symbol_no) ||
             (e1->right.sym == &symbol_no && e2->right.sym == &symbol_mod)))
            return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);
    }
    return 0;
}

static void elim_dups1(int type, struct expr **ep1, struct expr **ep2)
{
    struct expr *tmp;
    if ((*ep1)->type == type) {
        elim_dups1(type, &(*ep1)->left.expr, ep2);
        elim_dups1(type, &(*ep1)->right.expr, ep2);
        return;
    }
    if ((*ep2)->type == type) {
        elim_dups1(type, ep1, &(*ep2)->left.expr);
        elim_dups1(type, ep1, &(*ep2)->right.expr);
        return;
    }
    if (*ep1 == *ep2)
        return;
    switch ((*ep1)->type) {
    case E_OR:
    case E_AND:
        elim_dups1((*ep1)->type, ep1, ep1);
        break;
    default:
        break;
    }
    if (type == E_OR) {
        tmp = expr_join_or(*ep1, *ep2);
        if (tmp) {
            *ep1 = expr_alloc_symbol(&symbol_no);
            *ep2 = tmp;
            trans_count = trans_count + 1;
        }
    } else if (type == E_AND) {
        tmp = expr_join_and(*ep1, *ep2);
        if (tmp) {
            *ep1 = expr_alloc_symbol(&symbol_yes);
            *ep2 = tmp;
            trans_count = trans_count + 1;
        }
    }
}

static struct expr *expr_eliminate_dups(struct expr *e)
{
    int oldcount;
    if (!e)
        return e;
    oldcount = trans_count;
    for (;;) {
        trans_count = 0;
        switch (e->type) {
        case E_OR:
        case E_AND:
            elim_dups1(e->type, &e, &e);
            break;
        default:
            break;
        }
        if (!trans_count)
            break;
        e = expr_eliminate_yn(e);
    }
    trans_count = oldcount;
    return e;
}

static struct expr *expr_transform(struct expr *e)
{
    struct expr *tmp;
    if (!e)
        return 0;
    switch (e->type) {
    case E_EQUAL:
    case E_GEQ:
    case E_GTH:
    case E_LEQ:
    case E_LTH:
    case E_UNEQUAL:
    case E_SYMBOL:
    case E_LIST:
        break;
    default:
        e->left.expr = expr_transform(e->left.expr);
        e->right.expr = expr_transform(e->right.expr);
        break;
    }
    switch (e->type) {
    case E_EQUAL:
        if (e->left.sym->type != S_BOOLEAN)
            break;
        if (e->right.sym == &symbol_no) {
            e->type = E_NOT;
            e->left.expr = expr_alloc_symbol(e->left.sym);
            e->right.sym = 0;
            break;
        }
        if (e->right.sym == &symbol_mod) {
            printf("boolean symbol %s tested for 'm'? test forced to 'n'\n", e->left.sym->name);
            e->type = E_SYMBOL;
            e->left.sym = &symbol_no;
            e->right.sym = 0;
            break;
        }
        if (e->right.sym == &symbol_yes) {
            e->type = E_SYMBOL;
            e->right.sym = 0;
            break;
        }
        break;
    case E_UNEQUAL:
        if (e->left.sym->type != S_BOOLEAN)
            break;
        if (e->right.sym == &symbol_no) {
            e->type = E_SYMBOL;
            e->right.sym = 0;
            break;
        }
        if (e->right.sym == &symbol_mod) {
            printf("boolean symbol %s tested for 'm'? test forced to 'y'\n", e->left.sym->name);
            e->type = E_SYMBOL;
            e->left.sym = &symbol_yes;
            e->right.sym = 0;
            break;
        }
        if (e->right.sym == &symbol_yes) {
            e->type = E_NOT;
            e->left.expr = expr_alloc_symbol(e->left.sym);
            e->right.sym = 0;
            break;
        }
        break;
    case E_NOT:
        switch (e->left.expr->type) {
        case E_NOT:
            e = e->left.expr->left.expr;
            e = expr_transform(e);
            break;
        case E_EQUAL:
        case E_UNEQUAL:
            e = e->left.expr;
            e->type = e->type == E_EQUAL ? E_UNEQUAL : E_EQUAL;
            break;
        case E_LEQ:
        case E_GEQ:
            e = e->left.expr;
            e->type = e->type == E_LEQ ? E_GTH : E_LTH;
            break;
        case E_LTH:
        case E_GTH:
            e = e->left.expr;
            e->type = e->type == E_LTH ? E_GEQ : E_LEQ;
            break;
        case E_OR:
            tmp = e->left.expr;
            e->type = E_AND;
            e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
            tmp->type = E_NOT;
            tmp->right.expr = 0;
            e = expr_transform(e);
            break;
        case E_AND:
            tmp = e->left.expr;
            e->type = E_OR;
            e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
            tmp->type = E_NOT;
            tmp->right.expr = 0;
            e = expr_transform(e);
            break;
        case E_SYMBOL:
            if (e->left.expr->left.sym == &symbol_yes) {
                e = e->left.expr;
                e->type = E_SYMBOL;
                e->left.sym = &symbol_no;
                break;
            }
            if (e->left.expr->left.sym == &symbol_mod) {
                e = e->left.expr;
                e->type = E_SYMBOL;
                e->left.sym = &symbol_mod;
                break;
            }
            if (e->left.expr->left.sym == &symbol_no) {
                e = e->left.expr;
                e->type = E_SYMBOL;
                e->left.sym = &symbol_yes;
                break;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return e;
}

static int expr_contains_symbol(struct expr *dep, struct sym *s)
{
    if (!dep)
        return 0;
    switch (dep->type) {
    case E_AND:
    case E_OR:
        return expr_contains_symbol(dep->left.expr, s) ||
               expr_contains_symbol(dep->right.expr, s);
    case E_SYMBOL:
        return dep->left.sym == s;
    case E_EQUAL:
    case E_GEQ:
    case E_GTH:
    case E_LEQ:
    case E_LTH:
    case E_UNEQUAL:
        return dep->left.sym == s || dep->right.sym == s;
    case E_NOT:
        return expr_contains_symbol(dep->left.expr, s);
    default:
        break;
    }
    return 0;
}

static int expr_depends_symbol(struct expr *dep, struct sym *s)
{
    if (!dep)
        return 0;
    switch (dep->type) {
    case E_AND:
        return expr_depends_symbol(dep->left.expr, s) ||
               expr_depends_symbol(dep->right.expr, s);
    case E_SYMBOL:
        return dep->left.sym == s;
    case E_EQUAL:
        if (dep->left.sym == s) {
            if (dep->right.sym == &symbol_yes || dep->right.sym == &symbol_mod)
                return 1;
        }
        break;
    case E_UNEQUAL:
        if (dep->left.sym == s) {
            if (dep->right.sym == &symbol_no)
                return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

/* 式に「記号 type 値」の比較を明示して入れる (A -> A!=n など) */
static struct expr *expr_trans_compare(struct expr *e, int type, struct sym *s)
{
    struct expr *e1;
    struct expr *e2;
    if (!e) {
        e = expr_alloc_symbol(s);
        if (type == E_UNEQUAL)
            e = expr_alloc_one(E_NOT, e);
        return e;
    }
    switch (e->type) {
    case E_AND:
        e1 = expr_trans_compare(e->left.expr, E_EQUAL, s);
        e2 = expr_trans_compare(e->right.expr, E_EQUAL, s);
        if (s == &symbol_yes)
            e = expr_alloc_two(E_AND, e1, e2);
        if (s == &symbol_no)
            e = expr_alloc_two(E_OR, e1, e2);
        if (type == E_UNEQUAL)
            e = expr_alloc_one(E_NOT, e);
        return e;
    case E_OR:
        e1 = expr_trans_compare(e->left.expr, E_EQUAL, s);
        e2 = expr_trans_compare(e->right.expr, E_EQUAL, s);
        if (s == &symbol_yes)
            e = expr_alloc_two(E_OR, e1, e2);
        if (s == &symbol_no)
            e = expr_alloc_two(E_AND, e1, e2);
        if (type == E_UNEQUAL)
            e = expr_alloc_one(E_NOT, e);
        return e;
    case E_NOT:
        return expr_trans_compare(e->left.expr, type == E_EQUAL ? E_UNEQUAL : E_EQUAL, s);
    case E_UNEQUAL:
    case E_LTH:
    case E_LEQ:
    case E_GTH:
    case E_GEQ:
    case E_EQUAL:
        if (type == E_EQUAL) {
            if (s == &symbol_yes)
                return expr_copy(e);
            if (s == &symbol_mod)
                return expr_alloc_symbol(&symbol_no);
            if (s == &symbol_no)
                return expr_alloc_one(E_NOT, expr_copy(e));
        } else {
            if (s == &symbol_yes)
                return expr_alloc_one(E_NOT, expr_copy(e));
            if (s == &symbol_mod)
                return expr_alloc_symbol(&symbol_yes);
            if (s == &symbol_no)
                return expr_copy(e);
        }
        break;
    case E_SYMBOL:
        return expr_alloc_comp(type, e->left.sym, s);
    default:
        break;
    }
    return 0;
}

/* 'm' を 'm' && MODULES に書き換える */
static struct expr *rewrite_m(struct expr *e)
{
    if (!e)
        return e;
    switch (e->type) {
    case E_NOT:
        e->left.expr = rewrite_m(e->left.expr);
        break;
    case E_OR:
    case E_AND:
        e->left.expr = rewrite_m(e->left.expr);
        e->right.expr = rewrite_m(e->right.expr);
        break;
    case E_SYMBOL:
        if (e->left.sym == &symbol_mod)
            return expr_alloc_and(e, expr_alloc_symbol(modules_sym));
        break;
    default:
        break;
    }
    return e;
}

/* ---- 記号 ---- */

static void sym_init_consts(void)
{
    symbol_yes.name = "y";
    symbol_yes.type = S_TRISTATE;
    symbol_yes.curr.val = "y";
    symbol_yes.curr.tri = yes;
    symbol_yes.flags = SYMBOL_CONST | SYMBOL_VALID;
    symbol_mod.name = "m";
    symbol_mod.type = S_TRISTATE;
    symbol_mod.curr.val = "m";
    symbol_mod.curr.tri = mod;
    symbol_mod.flags = SYMBOL_CONST | SYMBOL_VALID;
    symbol_no.name = "n";
    symbol_no.type = S_TRISTATE;
    symbol_no.curr.val = "n";
    symbol_no.curr.tri = no;
    symbol_no.flags = SYMBOL_CONST | SYMBOL_VALID;
    symbol_empty.name = "";
    symbol_empty.curr.val = "";
    symbol_empty.curr.tri = no;
    symbol_empty.flags = SYMBOL_VALID;
}

/* FNV-1 (32 bit) */
static unsigned strhash(const char *s)
{
    unsigned hash;
    hash = 2166136261U;
    for (; *s; s++)
        hash = (hash ^ (unsigned char)*s) * 0x01000193U;
    return hash;
}

static struct sym *sym_lookup(const char *name, int flags)
{
    struct sym *s;
    char *new_name;
    unsigned hash;
    if (name) {
        if (name[0] && !name[1]) {
            switch (name[0]) {
            case 'y': return &symbol_yes;
            case 'm': return &symbol_mod;
            case 'n': return &symbol_no;
            }
        }
        hash = strhash(name) % SYMBOL_HASHSIZE;
        for (s = symbol_hash[hash]; s; s = s->next) {
            if (s->name && !strcmp(s->name, name) &&
                (flags ? s->flags & flags
                       : !(s->flags & (SYMBOL_CONST | SYMBOL_CHOICE))))
                return s;
        }
        new_name = xstrdup(name);
    } else {
        new_name = 0;
        hash = 0;
    }
    s = xcalloc(sizeof(*s));
    s->name = new_name;
    s->type = S_UNKNOWN;
    s->flags = s->flags | flags;
    s->next = symbol_hash[hash];
    symbol_hash[hash] = s;
    return s;
}

static struct sym *sym_find(const char *name)
{
    struct sym *s;
    unsigned hash;
    if (!name)
        return 0;
    if (name[0] && !name[1]) {
        switch (name[0]) {
        case 'y': return &symbol_yes;
        case 'm': return &symbol_mod;
        case 'n': return &symbol_no;
        }
    }
    hash = strhash(name) % SYMBOL_HASHSIZE;
    for (s = symbol_hash[hash]; s; s = s->next) {
        if (s->name && !strcmp(s->name, name) && !(s->flags & SYMBOL_CONST))
            break;
    }
    return s;
}

#define sym_is_choice(s)       (((s)->flags & SYMBOL_CHOICE) != 0)
#define sym_is_choice_value(s) (((s)->flags & SYMBOL_CHOICEVAL) != 0)
#define sym_is_optional(s)     (((s)->flags & SYMBOL_OPTIONAL) != 0)
#define sym_has_value(s)       (((s)->flags & SYMBOL_DEF_USER) != 0)
#define sym_get_tristate_value(s) ((s)->curr.tri)

static struct prop *prop_alloc(int type, struct sym *s, int lineno)
{
    struct prop *p;
    struct prop **pp;
    p = xcalloc(sizeof(*p));
    p->type = type;
    p->sym = s;
    p->file = current_file;
    p->lineno = lineno;
    if (s) {
        for (pp = &s->prop; *pp; pp = &(*pp)->next)
            ;
        *pp = p;
    }
    return p;
}

static struct sym *prop_get_symbol(struct prop *p)
{
    if (p->expr && (p->expr->type == E_SYMBOL || p->expr->type == E_LIST))
        return p->expr->left.sym;
    return 0;
}

static struct prop *sym_get_choice_prop(struct sym *s)
{
    struct prop *p;
    for (p = s->prop; p; p = p->next)
        if (p->type == P_CHOICE)
            return p;
    return 0;
}

static int sym_get_type(struct sym *s)
{
    int type;
    type = s->type;
    if (type == S_TRISTATE) {
        if (sym_is_choice_value(s) && s->visible == yes)
            type = S_BOOLEAN;
        else if (modules_val == no)
            type = S_BOOLEAN;
    }
    return type;
}

static struct prop *sym_get_default_prop(struct sym *s)
{
    struct prop *p;
    for (p = s->prop; p; p = p->next) {
        if (p->type != P_DEFAULT)
            continue;
        p->visible.tri = expr_calc_value(p->visible.expr);
        if (p->visible.tri != no)
            return p;
    }
    return 0;
}

static struct prop *sym_get_range_prop(struct sym *s)
{
    struct prop *p;
    for (p = s->prop; p; p = p->next) {
        if (p->type != P_RANGE)
            continue;
        p->visible.tri = expr_calc_value(p->visible.expr);
        if (p->visible.tri != no)
            return p;
    }
    return 0;
}

static long long sym_get_range_val(struct sym *s, int base)
{
    sym_calc_value(s);
    switch (s->type) {
    case S_INT:
        base = 10;
        break;
    case S_HEX:
        base = 16;
        break;
    default:
        break;
    }
    return strtoll(s->curr.val, 0, base);
}

static void sym_validate_range(struct sym *s)
{
    struct prop *p;
    struct sym *range_sym;
    int base;
    long long val;
    long long val2;
    switch (s->type) {
    case S_INT:
        base = 10;
        break;
    case S_HEX:
        base = 16;
        break;
    default:
        return;
    }
    p = sym_get_range_prop(s);
    if (!p)
        return;
    val = strtoll(s->curr.val, 0, base);
    range_sym = p->expr->left.sym;
    val2 = sym_get_range_val(range_sym, base);
    if (val >= val2) {
        range_sym = p->expr->right.sym;
        val2 = sym_get_range_val(range_sym, base);
        if (val <= val2)
            return;
    }
    s->curr.val = range_sym->curr.val;
}

static void sym_calc_visibility(struct sym *s)
{
    struct prop *p;
    struct sym *choice_sym;
    int tri;
    tri = no;
    choice_sym = 0;
    if (sym_is_choice_value(s))
        choice_sym = prop_get_symbol(sym_get_choice_prop(s));
    for (p = s->prop; p; p = p->next) {
        if (!p->text)
            continue;
        p->visible.tri = expr_calc_value(p->visible.expr);
        if (choice_sym && s->type == S_TRISTATE &&
            p->visible.tri == mod && choice_sym->curr.tri == yes)
            p->visible.tri = no;
        tri = EXPR_OR(tri, p->visible.tri);
    }
    if (tri == mod && (s->type != S_TRISTATE || modules_val == no))
        tri = yes;
    if (s->visible != tri)
        s->visible = tri;
    if (sym_is_choice_value(s))
        return;
    tri = yes;
    if (s->dir_dep.expr)
        tri = expr_calc_value(s->dir_dep.expr);
    if (tri == mod && sym_get_type(s) == S_BOOLEAN)
        tri = yes;
    s->dir_dep.tri = tri;
    tri = no;
    if (s->rev_dep.expr)
        tri = expr_calc_value(s->rev_dep.expr);
    if (tri == mod && sym_get_type(s) == S_BOOLEAN)
        tri = yes;
    s->rev_dep.tri = tri;
    tri = no;
    if (s->implied.expr && s->dir_dep.tri != no)
        tri = expr_calc_value(s->implied.expr);
    if (tri == mod && sym_get_type(s) == S_BOOLEAN)
        tri = yes;
    s->implied.tri = tri;
}

static struct sym *sym_choice_default(struct sym *s)
{
    struct sym *def_sym;
    struct prop *p;
    struct expr *e;
    for (p = s->prop; p; p = p->next) {
        if (p->type != P_DEFAULT)
            continue;
        p->visible.tri = expr_calc_value(p->visible.expr);
        if (p->visible.tri == no)
            continue;
        def_sym = prop_get_symbol(p);
        if (def_sym->visible != no)
            return def_sym;
    }
    p = sym_get_choice_prop(s);
    for (e = p->expr; e && (def_sym = e->right.sym) != 0; e = e->left.expr)
        if (def_sym->visible != no)
            return def_sym;
    return 0;
}

static struct sym *sym_calc_choice(struct sym *s)
{
    struct sym *def_sym;
    struct prop *p;
    struct expr *e;
    int flags;
    flags = s->flags;
    p = sym_get_choice_prop(s);
    for (e = p->expr; e && (def_sym = e->right.sym) != 0; e = e->left.expr) {
        sym_calc_visibility(def_sym);
        if (def_sym->visible != no)
            flags = flags & def_sym->flags;
    }
    s->flags = s->flags & (flags | ~SYMBOL_DEF_USER);
    def_sym = s->def[S_DEF_USER].val;
    if (def_sym && def_sym->visible != no)
        return def_sym;
    def_sym = sym_choice_default(s);
    if (def_sym == 0)
        s->curr.tri = no;
    return def_sym;
}

static void set_all_choice_values(struct sym *csym)
{
    struct prop *p;
    struct sym *s;
    struct expr *e;
    p = sym_get_choice_prop(csym);
    for (e = p->expr; e && (s = e->right.sym) != 0; e = e->left.expr) {
        if (!sym_has_value(s))
            s->def[S_DEF_USER].tri = no;
    }
    csym->flags = csym->flags | SYMBOL_DEF_USER;
    csym->flags = csym->flags & ~(SYMBOL_VALID | SYMBOL_NEED_SET_CHOICE_VALUES);
}

static void sym_warn_unmet_dep(struct sym *s)
{
    fprintf(stderr, "\nWARNING: unmet direct dependencies detected for %s\n", s->name);
}

static void sym_calc_value(struct sym *s)
{
    struct sval newval;
    struct sval oldval;
    struct prop *p;
    struct expr *e;
    struct sym *ds;
    struct sym *choice_sym;
    if (!s)
        return;
    if (s->flags & SYMBOL_VALID)
        return;
    if (sym_is_choice_value(s) && (s->flags & SYMBOL_NEED_SET_CHOICE_VALUES)) {
        s->flags = s->flags & ~SYMBOL_NEED_SET_CHOICE_VALUES;
        p = sym_get_choice_prop(s);
        sym_calc_value(prop_get_symbol(p));
    }
    s->flags = s->flags | SYMBOL_VALID;
    oldval = s->curr;
    switch (s->type) {
    case S_INT:
    case S_HEX:
    case S_STRING:
        newval = symbol_empty.curr;
        break;
    case S_BOOLEAN:
    case S_TRISTATE:
        newval = symbol_no.curr;
        break;
    default:
        s->curr.val = s->name;
        s->curr.tri = no;
        return;
    }
    s->flags = s->flags & ~SYMBOL_WRITE;
    sym_calc_visibility(s);
    if (s->visible != no)
        s->flags = s->flags | SYMBOL_WRITE;
    s->curr = newval;
    switch (sym_get_type(s)) {
    case S_BOOLEAN:
    case S_TRISTATE:
        if (sym_is_choice_value(s) && s->visible == yes) {
            p = sym_get_choice_prop(s);
            newval.tri = (prop_get_symbol(p)->curr.val == s) ? yes : no;
        } else {
            if (s->visible != no && sym_has_value(s)) {
                newval.tri = EXPR_AND(s->def[S_DEF_USER].tri, s->visible);
            } else {
                if (s->rev_dep.tri != no)
                    s->flags = s->flags | SYMBOL_WRITE;
                if (!sym_is_choice(s)) {
                    p = sym_get_default_prop(s);
                    if (p) {
                        newval.tri = EXPR_AND(expr_calc_value(p->expr), p->visible.tri);
                        if (newval.tri != no)
                            s->flags = s->flags | SYMBOL_WRITE;
                    }
                    if (s->implied.tri != no) {
                        s->flags = s->flags | SYMBOL_WRITE;
                        newval.tri = EXPR_OR(newval.tri, s->implied.tri);
                    }
                }
            }
            if (s->dir_dep.tri < s->rev_dep.tri)
                sym_warn_unmet_dep(s);
            newval.tri = EXPR_OR(newval.tri, s->rev_dep.tri);
        }
        if (newval.tri == mod &&
            (sym_get_type(s) == S_BOOLEAN || s->implied.tri == yes))
            newval.tri = yes;
        break;
    case S_STRING:
    case S_HEX:
    case S_INT:
        if (s->visible != no && sym_has_value(s)) {
            newval.val = s->def[S_DEF_USER].val;
            break;
        }
        p = sym_get_default_prop(s);
        if (p) {
            ds = prop_get_symbol(p);
            if (ds) {
                s->flags = s->flags | SYMBOL_WRITE;
                sym_calc_value(ds);
                newval.val = ds->curr.val;
            }
        }
        break;
    default:
        break;
    }
    s->curr = newval;
    if (sym_is_choice(s) && newval.tri == yes)
        s->curr.val = sym_calc_choice(s);
    sym_validate_range(s);
    if (oldval.val != s->curr.val || oldval.tri != s->curr.tri) {
        if (modules_sym == s)
            modules_val = modules_sym->curr.tri;
    }
    if (sym_is_choice(s)) {
        p = sym_get_choice_prop(s);
        for (e = p->expr; e && (choice_sym = e->right.sym) != 0; e = e->left.expr) {
            if ((s->flags & SYMBOL_WRITE) && choice_sym->visible != no)
                choice_sym->flags = choice_sym->flags | SYMBOL_WRITE;
        }
    }
    if (s->flags & SYMBOL_NO_WRITE)
        s->flags = s->flags & ~SYMBOL_WRITE;
    if (s->flags & SYMBOL_NEED_SET_CHOICE_VALUES)
        set_all_choice_values(s);
}

static void sym_set_change_count(int count)
{
    sym_change_count = count;
}

static void sym_add_change_count(int count)
{
    sym_set_change_count(count + sym_change_count);
}

static void sym_clear_all_valid(void)
{
    struct sym *s;
    int i;
    for (i = 0; i < SYMBOL_HASHSIZE; i++)
        for (s = symbol_hash[i]; s; s = s->next)
            s->flags = s->flags & ~SYMBOL_VALID;
    sym_add_change_count(1);
    sym_calc_value(modules_sym);
}

static int sym_string_valid(struct sym *s, const char *str)
{
    int ch;
    switch (s->type) {
    case S_STRING:
        return 1;
    case S_INT:
        ch = (unsigned char)*str++;
        if (ch == '-')
            ch = (unsigned char)*str++;
        if (!isdigit(ch))
            return 0;
        if (ch == '0' && *str != 0)
            return 0;
        while ((ch = (unsigned char)*str++) != 0) {
            if (!isdigit(ch))
                return 0;
        }
        return 1;
    case S_HEX:
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
            str += 2;
        ch = (unsigned char)*str++;
        do {
            if (!isxdigit(ch))
                return 0;
        } while ((ch = (unsigned char)*str++) != 0);
        return 1;
    case S_BOOLEAN:
    case S_TRISTATE:
        switch (str[0]) {
        case 'y': case 'Y':
        case 'm': case 'M':
        case 'n': case 'N':
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static int sym_tristate_within_range(struct sym *s, int val)
{
    int type;
    type = sym_get_type(s);
    if (s->visible == no)
        return 0;
    if (type != S_BOOLEAN && type != S_TRISTATE)
        return 0;
    if (type == S_BOOLEAN && val == mod)
        return 0;
    if (s->visible <= s->rev_dep.tri)
        return 0;
    if (s->implied.tri == yes && val == mod)
        return 0;
    if (sym_is_choice_value(s) && s->visible == yes)
        return val == yes;
    return val >= s->rev_dep.tri && val <= s->visible;
}

static int sym_string_within_range(struct sym *s, const char *str)
{
    struct prop *p;
    long long val;
    switch (s->type) {
    case S_STRING:
        return sym_string_valid(s, str);
    case S_INT:
        if (!sym_string_valid(s, str))
            return 0;
        p = sym_get_range_prop(s);
        if (!p)
            return 1;
        val = strtoll(str, 0, 10);
        return val >= sym_get_range_val(p->expr->left.sym, 10) &&
               val <= sym_get_range_val(p->expr->right.sym, 10);
    case S_HEX:
        if (!sym_string_valid(s, str))
            return 0;
        p = sym_get_range_prop(s);
        if (!p)
            return 1;
        val = strtoll(str, 0, 16);
        return val >= sym_get_range_val(p->expr->left.sym, 16) &&
               val <= sym_get_range_val(p->expr->right.sym, 16);
    case S_BOOLEAN:
    case S_TRISTATE:
        switch (str[0]) {
        case 'y': case 'Y':
            return sym_tristate_within_range(s, yes);
        case 'm': case 'M':
            return sym_tristate_within_range(s, mod);
        case 'n': case 'N':
            return sym_tristate_within_range(s, no);
        }
        return 0;
    default:
        return 0;
    }
}

static const char *sym_get_string_value(struct sym *s)
{
    switch (s->type) {
    case S_BOOLEAN:
    case S_TRISTATE:
        switch (sym_get_tristate_value(s)) {
        case no:
            return "n";
        case mod:
            return "m";
        default:
            return "y";
        }
    default:
        break;
    }
    return (const char *)s->curr.val;
}

static int sym_is_changeable(struct sym *s)
{
    return s->visible > s->rev_dep.tri;
}

static const char *sym_get_string_default(struct sym *s)
{
    struct prop *p;
    struct sym *ds;
    const char *str;
    int val;
    sym_calc_visibility(s);
    sym_calc_value(modules_sym);
    val = no;
    str = "";
    p = sym_get_default_prop(s);
    if (p) {
        switch (s->type) {
        case S_BOOLEAN:
        case S_TRISTATE:
            val = EXPR_AND(expr_calc_value(p->expr), p->visible.tri);
            break;
        default:
            ds = prop_get_symbol(p);
            if (ds) {
                sym_calc_value(ds);
                str = (const char *)ds->curr.val;
            }
            break;
        }
    }
    val = EXPR_OR(val, s->rev_dep.tri);
    if (val == mod)
        if (!sym_is_choice_value(s) && modules_sym->curr.tri == no)
            val = yes;
    if (s->type == S_BOOLEAN && val == mod)
        val = yes;
    if (val < s->implied.tri)
        val = s->implied.tri;
    switch (s->type) {
    case S_BOOLEAN:
    case S_TRISTATE:
        switch (val) {
        case no: return "n";
        case mod: return "m";
        default: return "y";
        }
    case S_INT:
    case S_HEX:
    case S_STRING:
        return str;
    default:
        break;
    }
    return "";
}

/* 文字列を "..." で囲み，" と \ の前に \ を置く */
static char *sym_escape_string_value(const char *in)
{
    struct sbuf b;
    const char *p;
    sb_init(&b);
    sb_add(&b, "\"");
    for (p = in; *p; p++) {
        if (*p == '"' || *p == '\\')
            sb_add(&b, "\\");
        sb_addn(&b, p, 1);
    }
    sb_add(&b, "\"");
    return b.s;
}

/* 比較の両辺を値として読む。数として読めなければ文字列として比べる */
#define K_STRING   0
#define K_SIGNED   1
#define K_UNSIGNED 2

static int expr_parse_string(const char *str, int type, long long *sv, unsigned long long *uv)
{
    char *tail;
    int kind;
    errno = 0;
    switch (type) {
    case S_BOOLEAN:
    case S_TRISTATE:
        *sv = !strcmp(str, "n") ? 0 :
              !strcmp(str, "m") ? 1 :
              !strcmp(str, "y") ? 2 : -1;
        return K_SIGNED;
    case S_INT:
        *sv = strtoll(str, &tail, 10);
        kind = K_SIGNED;
        break;
    case S_HEX:
        *uv = strtoull(str, &tail, 16);
        kind = K_UNSIGNED;
        break;
    default:
        *sv = strtoll(str, &tail, 0);
        kind = K_SIGNED;
        break;
    }
    return !errno && !*tail && tail > str && isxdigit((unsigned char)tail[-1])
           ? kind : K_STRING;
}

static int expr_calc_value(struct expr *e)
{
    int val1;
    int val2;
    const char *str1;
    const char *str2;
    int k1;
    int k2;
    long long ls;
    long long rs;
    unsigned long long lu;
    unsigned long long ru;
    int res;
    if (!e)
        return yes;
    switch (e->type) {
    case E_SYMBOL:
        sym_calc_value(e->left.sym);
        return e->left.sym->curr.tri;
    case E_AND:
        val1 = expr_calc_value(e->left.expr);
        val2 = expr_calc_value(e->right.expr);
        return EXPR_AND(val1, val2);
    case E_OR:
        val1 = expr_calc_value(e->left.expr);
        val2 = expr_calc_value(e->right.expr);
        return EXPR_OR(val1, val2);
    case E_NOT:
        val1 = expr_calc_value(e->left.expr);
        return EXPR_NOT(val1);
    case E_EQUAL:
    case E_GEQ:
    case E_GTH:
    case E_LEQ:
    case E_LTH:
    case E_UNEQUAL:
        break;
    default:
        printf("expr_calc_value: %d?\n", e->type);
        return no;
    }
    sym_calc_value(e->left.sym);
    sym_calc_value(e->right.sym);
    str1 = sym_get_string_value(e->left.sym);
    str2 = sym_get_string_value(e->right.sym);
    k1 = K_STRING;
    k2 = K_STRING;
    ls = 0;
    rs = 0;
    lu = 0;
    ru = 0;
    if (e->left.sym->type != S_STRING || e->right.sym->type != S_STRING) {
        k1 = expr_parse_string(str1, e->left.sym->type, &ls, &lu);
        k2 = expr_parse_string(str2, e->right.sym->type, &rs, &ru);
        /* 上流は符号つきと符号なしを 1 つの共用体で持つ。片方だけ
         * 符号なしで読んだときは，もう片方のビット列を符号なしとして比べる */
        if (k1 == K_SIGNED)
            lu = (unsigned long long)ls;
        else if (k1 == K_UNSIGNED)
            ls = (long long)lu;
        if (k2 == K_SIGNED)
            ru = (unsigned long long)rs;
        else if (k2 == K_UNSIGNED)
            rs = (long long)ru;
    }
    if (k1 == K_STRING || k2 == K_STRING)
        res = strcmp(str1, str2);
    else if (k1 == K_UNSIGNED || k2 == K_UNSIGNED)
        res = (lu > ru) - (lu < ru);
    else
        res = (ls > rs) - (ls < rs);
    switch (e->type) {
    case E_EQUAL:
        return res ? no : yes;
    case E_GEQ:
        return res >= 0 ? yes : no;
    case E_GTH:
        return res > 0 ? yes : no;
    case E_LEQ:
        return res <= 0 ? yes : no;
    case E_LTH:
        return res < 0 ? yes : no;
    case E_UNEQUAL:
        return res ? yes : no;
    default:
        break;
    }
    return no;
}

/* ---- 外部への出口 (OS ごとに違う) ---- */

/* 命令をシェルで実行し，標準出力を返す。末尾の改行を落とし，途中の改行は
 * 空白にする (上流の $(shell,...) と同じ。読むのは 4095 バイトまで) */
static char *run_shell(const char *cmd)
{
    FILE *p;
    char buf[4096];
    size_t nread;
    size_t i;
    fflush(stdout);
    p = popen(cmd, "r");
    if (!p)
        die("cannot run: %s", cmd);
    nread = fread(buf, 1, sizeof(buf), p);
    if (nread == sizeof(buf))
        nread = nread - 1;
    while (nread > 0 && buf[nread - 1] == '\n')
        nread = nread - 1;
    buf[nread] = 0;
    for (i = 0; i < nread; i++)
        if (buf[i] == '\n')
            buf[i] = ' ';
    pclose(p);
    return xstrdup(buf);
}

/* ---- 前処理 (変数と関数) ---- */

#define VAR_SIMPLE    0
#define VAR_RECURSIVE 1
#define VAR_APPEND    2

struct var {
    struct var *next;
    char *name;
    char *value;
    int flavor;
    int exp_count;
};

struct env {
    struct env *next;
    char *name;
    char *value;
};

static struct var *var_list;
static struct var **var_tail = &var_list;
static struct env *env_list;
static struct env **env_tail = &env_list;
static int yylineno;

static void pperror(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: ", current_file ? current_file->name : "<none>", yylineno);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static char *expand_string_with_args(const char *in, int argc, char **argv);

static char *env_expand(const char *name)
{
    struct env *e;
    const char *value;
    if (!*name)
        return 0;
    for (e = env_list; e; e = e->next)
        if (!strcmp(name, e->name))
            return xstrdup(e->value);
    value = getenv(name);
    if (!value)
        return 0;
    e = xcalloc(sizeof(*e));
    e->name = xstrdup(name);
    e->value = xstrdup(value);
    *env_tail = e;
    env_tail = &e->next;
    return xstrdup(value);
}

static struct var *variable_lookup(const char *name)
{
    struct var *v;
    for (v = var_list; v; v = v->next)
        if (!strcmp(name, v->name))
            return v;
    return 0;
}

static char *variable_expand(const char *name, int argc, char **argv)
{
    struct var *v;
    char *res;
    v = variable_lookup(name);
    if (!v)
        return 0;
    if (argc == 0 && v->exp_count)
        pperror("Recursive variable '%s' references itself (eventually)", name);
    if (v->exp_count > 1000)
        pperror("Too deep recursive expansion");
    v->exp_count = v->exp_count + 1;
    if (v->flavor == VAR_RECURSIVE)
        res = expand_string_with_args(v->value, argc, argv);
    else
        res = xstrdup(v->value);
    v->exp_count = v->exp_count - 1;
    return res;
}

static void variable_add(const char *name, const char *value, int flavor)
{
    struct var *v;
    char *new_value;
    char *joined;
    int append;
    append = 0;
    v = variable_lookup(name);
    if (v) {
        if (flavor == VAR_APPEND) {
            flavor = v->flavor;
            append = 1;
        }
    } else {
        if (flavor == VAR_APPEND)
            flavor = VAR_RECURSIVE;
        v = xcalloc(sizeof(*v));
        v->name = xstrdup(name);
        *var_tail = v;
        var_tail = &v->next;
    }
    v->flavor = flavor;
    if (flavor == VAR_SIMPLE)
        new_value = expand_string_with_args(value, 0, 0);
    else
        new_value = xstrdup(value);
    if (append) {
        joined = xmalloc(strlen(v->value) + strlen(new_value) + 2);
        strcpy(joined, v->value);
        strcat(joined, " ");
        strcat(joined, new_value);
        v->value = joined;
    } else {
        v->value = new_value;
    }
}

static char *function_expand(const char *name, int argc, char **argv)
{
    char buf[16];
    if (!strcmp(name, "error-if")) {
        if (argc != 2)
            pperror("wrong number of function arguments passed to '%s'", name);
        if (!strcmp(argv[0], "y"))
            pperror("%s", argv[1]);
        return xstrdup("");
    }
    if (!strcmp(name, "filename")) {
        if (argc != 0)
            pperror("too many function arguments passed to '%s'", name);
        return xstrdup(current_file->name);
    }
    if (!strcmp(name, "info")) {
        if (argc != 1)
            pperror("wrong number of function arguments passed to '%s'", name);
        printf("%s\n", argv[0]);
        return xstrdup("");
    }
    if (!strcmp(name, "lineno")) {
        if (argc != 0)
            pperror("too many function arguments passed to '%s'", name);
        sprintf(buf, "%d", yylineno);
        return xstrdup(buf);
    }
    if (!strcmp(name, "shell")) {
        if (argc != 1)
            pperror("wrong number of function arguments passed to '%s'", name);
        return run_shell(argv[0]);
    }
    if (!strcmp(name, "warning-if")) {
        if (argc != 2)
            pperror("wrong number of function arguments passed to '%s'", name);
        if (!strcmp(argv[0], "y"))
            fprintf(stderr, "%s:%d: %s\n", current_file->name, yylineno, argv[1]);
        return xstrdup("");
    }
    return 0;
}

#define FUNCTION_MAX_ARGS 16

/* $( ... ) の中身 (str から len バイト) を評価する */
static char *eval_clause(const char *str, size_t len, int argc, char **argv)
{
    char *tmp;
    char *name;
    char *res;
    char *endptr;
    char *prev;
    char *p;
    int new_argc;
    char *new_argv[FUNCTION_MAX_ARGS];
    int nest;
    int i;
    unsigned long n;
    new_argc = 0;
    nest = 0;
    tmp = xstrndup(str, len);
    n = strtoul(tmp, &endptr, 10);
    if (!*endptr && n > 0 && n <= (unsigned long)argc)
        return xstrdup(argv[n - 1]);
    prev = tmp;
    p = tmp;
    while (*p) {
        if (nest == 0 && *p == ',') {
            *p = 0;
            if (new_argc >= FUNCTION_MAX_ARGS)
                pperror("too many function arguments");
            new_argv[new_argc] = prev;
            new_argc = new_argc + 1;
            prev = p + 1;
        } else if (*p == '(') {
            nest = nest + 1;
        } else if (*p == ')') {
            nest = nest - 1;
        }
        p++;
    }
    if (new_argc >= FUNCTION_MAX_ARGS)
        pperror("too many function arguments");
    new_argv[new_argc] = prev;
    new_argc = new_argc + 1;
    name = expand_string_with_args(new_argv[0], argc, argv);
    new_argc = new_argc - 1;
    for (i = 0; i < new_argc; i++)
        new_argv[i] = expand_string_with_args(new_argv[i + 1], argc, argv);
    res = variable_expand(name, new_argc, new_argv);
    if (res)
        return res;
    res = function_expand(name, new_argc, new_argv);
    if (res)
        return res;
    if (new_argc == 0) {
        res = env_expand(name);
        if (res)
            return res;
    }
    return xstrdup("");
}

/* *str は '$' の次を指す。'(' でなければ "$" のまま */
static char *expand_dollar_with_args(const char **str, int argc, char **argv)
{
    const char *p;
    const char *q;
    int nest;
    p = *str;
    if (*p != '(') {
        *str = p;
        return xstrdup("$");
    }
    p++;
    q = p;
    nest = 0;
    while (*q) {
        if (*q == '(') {
            nest = nest + 1;
        } else if (*q == ')') {
            if (nest == 0)
                break;
            nest = nest - 1;
        }
        q++;
    }
    if (!*q)
        pperror("unterminated reference to '%s': missing ')'", p);
    *str = q + 1;
    return eval_clause(p, q - p, argc, argv);
}

static int is_end_of_str(int c)
{
    return !c;
}

static int is_end_of_token(int c)
{
    return !(isalnum(c) || c == '_' || c == '-');
}

static char *expand_string_by(const char **str, int (*is_end)(int), int argc, char **argv)
{
    const char *in;
    const char *p;
    char *expansion;
    struct sbuf out;
    sb_init(&out);
    p = *str;
    in = p;
    for (;;) {
        if (*p == '$') {
            sb_addn(&out, in, p - in);
            p++;
            expansion = expand_dollar_with_args(&p, argc, argv);
            sb_add(&out, expansion);
            in = p;
            continue;
        }
        if (is_end((unsigned char)*p))
            break;
        p++;
    }
    sb_addn(&out, in, p - in);
    *str = p;
    return out.s;
}

static char *expand_string_with_args(const char *in, int argc, char **argv)
{
    return expand_string_by(&in, is_end_of_str, argc, argv);
}

/* ---- 字句解析 ---- */

#define T_EOF            0
#define T_EOL            1
#define T_WORD           2
#define T_WORD_QUOTE     3
#define T_HELPTEXT       4
#define T_ASSIGN_VAL     5
#define T_ALLNOCONFIG_Y  10
#define T_BOOL           11
#define T_CHOICE         12
#define T_COMMENT        13
#define T_CONFIG         14
#define T_DEF_BOOL       15
#define T_DEF_TRISTATE   16
#define T_DEFAULT        17
#define T_DEFCONFIG_LIST 18
#define T_DEPENDS        19
#define T_ENDCHOICE      20
#define T_ENDIF          21
#define T_ENDMENU        22
#define T_HELP           23
#define T_HEX            24
#define T_IF             25
#define T_IMPLY          26
#define T_INT            27
#define T_MAINMENU       28
#define T_MENU           29
#define T_MENUCONFIG     30
#define T_MODULES        31
#define T_ON             32
#define T_OPTION         33
#define T_OPTIONAL       34
#define T_PROMPT         35
#define T_RANGE          36
#define T_SELECT         37
#define T_SOURCE         38
#define T_STRING         39
#define T_TRISTATE       40
#define T_VISIBLE        41
#define T_OR             50
#define T_AND            51
#define T_EQUAL          52
#define T_UNEQUAL        53
#define T_LESS           54
#define T_LESS_EQUAL     55
#define T_GREATER        56
#define T_GREATER_EQUAL  57
#define T_NOT            58
#define T_OPEN_PAREN     59
#define T_CLOSE_PAREN    60
#define T_COLON_EQUAL    61
#define T_PLUS_EQUAL     62

struct kw {
    const char *name;
    int token;
};

static struct kw keywords[] = {
    { "allnoconfig_y", T_ALLNOCONFIG_Y },
    { "bool", T_BOOL },
    { "choice", T_CHOICE },
    { "comment", T_COMMENT },
    { "config", T_CONFIG },
    { "def_bool", T_DEF_BOOL },
    { "def_tristate", T_DEF_TRISTATE },
    { "default", T_DEFAULT },
    { "defconfig_list", T_DEFCONFIG_LIST },
    { "depends", T_DEPENDS },
    { "endchoice", T_ENDCHOICE },
    { "endif", T_ENDIF },
    { "endmenu", T_ENDMENU },
    { "help", T_HELP },
    { "---help---", T_HELP },
    { "hex", T_HEX },
    { "if", T_IF },
    { "imply", T_IMPLY },
    { "int", T_INT },
    { "mainmenu", T_MAINMENU },
    { "menu", T_MENU },
    { "menuconfig", T_MENUCONFIG },
    { "modules", T_MODULES },
    { "on", T_ON },
    { "option", T_OPTION },
    { "optional", T_OPTIONAL },
    { "prompt", T_PROMPT },
    { "range", T_RANGE },
    { "select", T_SELECT },
    { "source", T_SOURCE },
    { "string", T_STRING },
    { "tristate", T_TRISTATE },
    { "visible", T_VISIBLE },
    { 0, 0 }
};

/* 読んでいるファイル。source で積み，終わりで戻る */
struct ibuf {
    struct ibuf *parent;
    char *data;
    size_t len;
    size_t pos;
};

#define L_INITIAL 0
#define L_ASSIGN  1
#define L_HELP    2

static struct ibuf *cur_buf;
static int lex_mode;
static int prev_token = T_EOL;
static int prev_prev_token = T_EOL;
static char *tokval;
static int stmt_lineno;
static int top_done;

/* name をまず作業ディレクトリから，無ければ $srctree の下から開く */
static FILE *zconf_fopen(const char *name)
{
    FILE *f;
    char *env;
    char *full;
    f = fopen(name, "r");
    if (!f && name && name[0] != '/') {
        env = getenv("srctree");
        if (env) {
            full = xmalloc(strlen(env) + strlen(name) + 2);
            strcpy(full, env);
            strcat(full, "/");
            strcat(full, name);
            f = fopen(full, "r");
        }
    }
    return f;
}

static struct kfile *file_lookup(const char *name)
{
    struct kfile *f;
    for (f = file_list; f; f = f->next)
        if (!strcmp(name, f->name))
            return f;
    f = xcalloc(sizeof(*f));
    f->name = xstrdup(name);
    f->next = file_list;
    file_list = f;
    return f;
}

static struct ibuf *read_ibuf(FILE *f)
{
    struct ibuf *b;
    size_t cap;
    size_t n;
    b = xcalloc(sizeof(*b));
    cap = 65536;
    b->data = xmalloc(cap);
    for (;;) {
        if (b->len + 4096 > cap) {
            cap = cap * 2;
            b->data = xrealloc(b->data, cap);
        }
        n = fread(b->data + b->len, 1, cap - b->len - 1, f);
        if (n == 0)
            break;
        b->len = b->len + n;
    }
    b->data[b->len] = 0;
    fclose(f);
    return b;
}

static void zconf_initscan(const char *name)
{
    FILE *f;
    f = zconf_fopen(name);
    if (!f)
        die("can't find file %s", name);
    cur_buf = read_ibuf(f);
    current_file = file_lookup(name);
    yylineno = 1;
}

static void zconf_nextfile(const char *name)
{
    struct kfile *iter;
    struct kfile *file;
    struct ibuf *b;
    FILE *f;
    file = file_lookup(name);
    f = zconf_fopen(file->name);
    if (!f)
        die("%s:%d: can't open file \"%s\"", current_file->name, stmt_lineno, file->name);
    b = read_ibuf(f);
    b->parent = cur_buf;
    cur_buf = b;
    current_file->lineno = yylineno;
    file->parent = current_file;
    for (iter = current_file; iter; iter = iter->parent)
        if (!strcmp(iter->name, file->name))
            die("Recursive inclusion detected: %s", file->name);
    yylineno = 1;
    current_file = file;
}

static void zconf_endfile(void)
{
    current_file = current_file->parent;
    if (current_file)
        yylineno = current_file->lineno;
    if (cur_buf->parent)
        cur_buf = cur_buf->parent;
    else
        top_done = 1;
}

static int is_word_char(int c)
{
    return isalnum(c) || c == '_' || c == '-';
}

/* 行末 (改行の手前) の位置 */
static size_t line_end(struct ibuf *b, size_t pos)
{
    while (pos < b->len && b->data[pos] != '\n')
        pos++;
    return pos;
}

/* 引用符で囲んだ文字列。中の $(...) は展開する */
static int lex_string(int q)
{
    struct ibuf *b;
    struct sbuf s;
    size_t start;
    size_t le;
    char *tmp;
    const char *p;
    char *res;
    int c;
    b = cur_buf;
    b->pos = b->pos + 1;
    sb_init(&s);
    for (;;) {
        if (b->pos >= b->len) {
            tokval = s.s;
            return T_WORD_QUOTE;
        }
        c = (unsigned char)b->data[b->pos];
        if (c == '$') {
            le = line_end(b, b->pos);
            tmp = xstrndup(b->data + b->pos + 1, le - b->pos - 1);
            p = tmp;
            res = expand_dollar_with_args(&p, 0, 0);
            sb_add(&s, res);
            b->pos = b->pos + 1 + (p - tmp);
            continue;
        }
        if (c == '\\') {
            if (b->pos + 1 < b->len && b->data[b->pos + 1] != '\n') {
                sb_addn(&s, b->data + b->pos + 1, 1);
                b->pos = b->pos + 2;
            } else {
                b->pos = b->pos + 1;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            b->pos = b->pos + 1;
            if (c == q) {
                tokval = s.s;
                return T_WORD_QUOTE;
            }
            sb_addn(&s, b->data + b->pos - 1, 1);
            continue;
        }
        if (c == '\n') {
            fprintf(stderr, "%s:%d:warning: multi-line strings not supported\n",
                    current_file->name, yylineno);
            tokval = s.s;
            return T_WORD_QUOTE;
        }
        start = b->pos;
        while (b->pos < b->len) {
            c = (unsigned char)b->data[b->pos];
            if (c == '$' || c == '\'' || c == '"' || c == '\\' || c == '\n')
                break;
            b->pos = b->pos + 1;
        }
        sb_addn(&s, b->data + start, b->pos - start);
    }
}

/* 語。$ を含めば行の残りから 1 語ぶんを展開する。空になれば語を出さない */
static int lex_word(void)
{
    struct ibuf *b;
    size_t start;
    size_t end;
    size_t le;
    int has_dollar;
    char *word;
    char *tmp;
    const char *p;
    int i;
    b = cur_buf;
    start = b->pos;
    end = start;
    has_dollar = 0;
    while (end < b->len) {
        if (b->data[end] == '$')
            has_dollar = 1;
        else if (!is_word_char((unsigned char)b->data[end]))
            break;
        end++;
    }
    if (!has_dollar) {
        word = xstrndup(b->data + start, end - start);
        b->pos = end;
        for (i = 0; keywords[i].name; i++)
            if (!strcmp(keywords[i].name, word))
                return keywords[i].token;
        tokval = word;
        return T_WORD;
    }
    le = line_end(b, start);
    tmp = xstrndup(b->data + start, le - start);
    p = tmp;
    word = expand_string_by(&p, is_end_of_token, 0, 0);
    b->pos = start + (p - tmp);
    if (!*word)
        return -1;
    tokval = word;
    return T_WORD;
}

/* help の本文の字下げ。first_ts は本文の最初の行の桁 */
static int help_first_ts;
static int help_last_ts;

static void lex_help_reset(void)
{
    help_first_ts = 0;
    help_last_ts = 0;
}

/* help の本文。字下げが本文より浅い行か，字下げの無い行で終わる。
 * 本文の中身は使わないので覚えない */
static int lex_help(void)
{
    struct ibuf *b;
    size_t ws;
    int ts;
    int c;
    b = cur_buf;
    for (;;) {
        if (b->pos >= b->len) {
            lex_mode = L_INITIAL;
            return T_HELPTEXT;
        }
        ws = b->pos;
        ts = 0;
        while (ws < b->len && (b->data[ws] == ' ' || b->data[ws] == '\t')) {
            if (b->data[ws] == '\t')
                ts = (ts & ~7) + 8;
            else
                ts++;
            ws++;
        }
        if (ws < b->len && b->data[ws] == '\n') {
            /* 空白だけの行。次の行が字下げ無しで始まれば終わる */
            b->pos = ws + 1;
            yylineno++;
            if (b->pos < b->len) {
                c = b->data[b->pos];
                if (c != ' ' && c != '\t' && c != '\n') {
                    lex_mode = L_INITIAL;
                    return T_HELPTEXT;
                }
            }
            continue;
        }
        if (ws > b->pos) {
            help_last_ts = ts;
            b->pos = ws;
            if (help_first_ts && ts < help_first_ts) {
                lex_mode = L_INITIAL;
                return T_HELPTEXT;
            }
            if (ws >= b->len)
                continue;
        }
        /* 本文の 1 行 */
        b->pos = line_end(b, b->pos);
        if (!help_first_ts)
            help_first_ts = help_last_ts;
    }
}

static int yylex1(void)
{
    struct ibuf *b;
    int c;
    int t;
    for (;;) {
        b = cur_buf;
        if (top_done)
            return T_EOF;
        if (lex_mode == L_HELP)
            return lex_help();
        if (b->pos >= b->len) {
            if (prev_token != T_EOL && prev_token != T_HELPTEXT)
                fprintf(stderr, "%s:%d:warning: no new line at end of file\n",
                        current_file->name, yylineno);
            lex_mode = L_INITIAL;
            if (current_file) {
                zconf_endfile();
                return T_EOL;
            }
            top_done = 1;
            return T_EOF;
        }
        c = (unsigned char)b->data[b->pos];
        if (lex_mode == L_ASSIGN) {
            if (c == '\n') {
                b->pos = b->pos + 1;
                yylineno++;
                lex_mode = L_INITIAL;
                return T_EOL;
            }
            if (c == ' ' || c == '\t') {
                b->pos = b->pos + 1;
                continue;
            }
            t = (int)line_end(b, b->pos);
            tokval = xstrndup(b->data + b->pos, (size_t)t - b->pos);
            b->pos = (size_t)t;
            return T_ASSIGN_VAL;
        }
        if (c == '#') {
            b->pos = line_end(b, b->pos);
            continue;
        }
        if (c == ' ' || c == '\t') {
            b->pos = b->pos + 1;
            continue;
        }
        if (c == '\\' && b->pos + 1 < b->len && b->data[b->pos + 1] == '\n') {
            b->pos = b->pos + 2;
            yylineno++;
            continue;
        }
        if (c == '\n') {
            b->pos = b->pos + 1;
            yylineno++;
            return T_EOL;
        }
        if (c == '"' || c == '\'')
            return lex_string(c);
        if (is_word_char(c) || c == '$') {
            t = lex_word();
            if (t < 0)
                continue;
            return t;
        }
        b->pos = b->pos + 1;
        switch (c) {
        case '|':
            if (b->data[b->pos] == '|') { b->pos++; return T_OR; }
            break;
        case '&':
            if (b->data[b->pos] == '&') { b->pos++; return T_AND; }
            break;
        case '=':
            return T_EQUAL;
        case '!':
            if (b->data[b->pos] == '=') { b->pos++; return T_UNEQUAL; }
            return T_NOT;
        case '<':
            if (b->data[b->pos] == '=') { b->pos++; return T_LESS_EQUAL; }
            return T_LESS;
        case '>':
            if (b->data[b->pos] == '=') { b->pos++; return T_GREATER_EQUAL; }
            return T_GREATER;
        case '(':
            return T_OPEN_PAREN;
        case ')':
            return T_CLOSE_PAREN;
        case ':':
            if (b->data[b->pos] == '=') { b->pos++; return T_COLON_EQUAL; }
            break;
        case '+':
            if (b->data[b->pos] == '=') { b->pos++; return T_PLUS_EQUAL; }
            break;
        default:
            break;
        }
        fprintf(stderr, "%s:%d:warning: ignoring unsupported character '%c'\n",
                current_file ? current_file->name : "<none>", yylineno, c);
    }
}

static int yylex(void)
{
    int t;
    for (;;) {
        t = yylex1();
        if ((prev_token == T_EOL || prev_token == T_HELPTEXT) && t == T_EOL)
            continue;
        break;
    }
    if (prev_token == T_EOL || prev_token == T_HELPTEXT)
        stmt_lineno = yylineno;
    if (prev_prev_token == T_EOL && prev_token == T_WORD &&
        (t == T_EQUAL || t == T_COLON_EQUAL || t == T_PLUS_EQUAL))
        lex_mode = L_ASSIGN;
    prev_prev_token = prev_token;
    prev_token = t;
    return t;
}

/* ---- menu ---- */

static void menu_warn(struct menu *m, const char *msg, const char *arg)
{
    fprintf(stderr, "%s:%d:warning: ", m->file ? m->file->name : "<none>", m->lineno);
    fprintf(stderr, msg, arg);
    fprintf(stderr, "\n");
}

static struct prop *menu_add_prop(int type, char *prompt, struct expr *expr, struct expr *dep);

static void menu_add_symbol(int type, struct sym *s, struct expr *dep)
{
    menu_add_prop(type, 0, expr_alloc_symbol(s), dep);
}

static void menu_add_entry(struct sym *s)
{
    struct menu *m;
    m = xcalloc(sizeof(*m));
    m->sym = s;
    m->parent = current_menu;
    m->file = current_file;
    m->lineno = stmt_lineno;
    *last_entry_ptr = m;
    last_entry_ptr = &m->next;
    current_entry = m;
    if (s)
        menu_add_symbol(P_SYMBOL, s, 0);
}

static struct menu *menu_add_menu(void)
{
    last_entry_ptr = &current_entry->list;
    current_menu = current_entry;
    return current_menu;
}

static void menu_end_menu(void)
{
    last_entry_ptr = &current_menu->next;
    current_menu = current_menu->parent;
}

static void menu_add_dep(struct expr *dep)
{
    current_entry->dep = expr_alloc_and(current_entry->dep, dep);
}

static void menu_set_type(int type)
{
    struct sym *s;
    s = current_entry->sym;
    if (s->type == type)
        return;
    if (s->type == S_UNKNOWN) {
        s->type = type;
        return;
    }
    menu_warn(current_entry, "ignoring type redefinition of '%s'", s->name ? s->name : "<choice>");
}

static struct prop *menu_add_prop(int type, char *prompt, struct expr *expr, struct expr *dep)
{
    struct prop *p;
    struct menu *m;
    p = prop_alloc(type, current_entry->sym, stmt_lineno);
    p->menu = current_entry;
    p->expr = expr;
    p->visible.expr = dep;
    if (prompt) {
        if (isspace((unsigned char)*prompt)) {
            fprintf(stderr, "%s:%d:warning: leading whitespace ignored\n",
                    current_file->name, stmt_lineno);
            while (isspace((unsigned char)*prompt))
                prompt++;
        }
        if (current_entry->prompt && current_entry != &rootmenu)
            fprintf(stderr, "%s:%d:warning: prompt redefined\n",
                    current_file->name, stmt_lineno);
        if (type == P_PROMPT) {
            m = current_entry;
            while ((m = m->parent) != 0) {
                if (!m->visibility)
                    continue;
                p->visible.expr = expr_alloc_and(p->visible.expr, expr_copy(m->visibility));
            }
        }
        current_entry->prompt = p;
    }
    p->text = prompt;
    return p;
}

static void menu_add_prompt(int type, char *prompt, struct expr *dep)
{
    menu_add_prop(type, prompt, 0, dep);
}

static void menu_add_visibility(struct expr *e)
{
    current_entry->visibility = expr_alloc_and(current_entry->visibility, e);
}

static void menu_add_expr(int type, struct expr *e, struct expr *dep)
{
    menu_add_prop(type, 0, e, dep);
}

static void menu_finalize(struct menu *parent)
{
    struct menu *m;
    struct menu *last_menu;
    struct sym *s;
    struct prop *p;
    struct expr *parentdep;
    struct expr *basedep;
    struct expr *dep;
    struct expr *dep2;
    struct expr **ep;
    struct sym *es;
    s = parent->sym;
    if (parent->list) {
        if (s && sym_is_choice(s)) {
            if (s->type == S_UNKNOWN) {
                current_entry = parent;
                for (m = parent->list; m; m = m->next) {
                    if (m->sym && m->sym->type != S_UNKNOWN) {
                        menu_set_type(m->sym->type);
                        break;
                    }
                }
            }
            for (m = parent->list; m; m = m->next) {
                current_entry = m;
                if (m->sym && m->sym->type == S_UNKNOWN)
                    menu_set_type(s->type);
            }
            parentdep = expr_alloc_symbol(s);
        } else if (parent->prompt)
            parentdep = parent->prompt->visible.expr;
        else
            parentdep = parent->dep;

        for (m = parent->list; m; m = m->next) {
            basedep = rewrite_m(m->dep);
            basedep = expr_transform(basedep);
            basedep = expr_alloc_and(expr_copy(parentdep), basedep);
            basedep = expr_eliminate_dups(basedep);
            m->dep = basedep;
            if (m->sym)
                p = m->sym->prop;
            else
                p = m->prompt;
            for (; p; p = p->next) {
                if (p->menu != m)
                    continue;
                dep = rewrite_m(p->visible.expr);
                dep = expr_transform(dep);
                dep = expr_alloc_and(expr_copy(basedep), dep);
                dep = expr_eliminate_dups(dep);
                p->visible.expr = dep;
                if (p->type == P_SELECT) {
                    es = prop_get_symbol(p);
                    es->rev_dep.expr = expr_alloc_or(es->rev_dep.expr,
                        expr_alloc_and(expr_alloc_symbol(m->sym), expr_copy(dep)));
                } else if (p->type == P_IMPLY) {
                    es = prop_get_symbol(p);
                    es->implied.expr = expr_alloc_or(es->implied.expr,
                        expr_alloc_and(expr_alloc_symbol(m->sym), expr_copy(dep)));
                }
            }
        }
        for (m = parent->list; m; m = m->next)
            menu_finalize(m);
    } else if (s) {
        /* 自動の副 menu: s に続く項目のうち s に依存するものを s の下へ */
        basedep = parent->prompt ? parent->prompt->visible.expr : 0;
        basedep = expr_trans_compare(basedep, E_UNEQUAL, &symbol_no);
        basedep = expr_eliminate_dups(expr_transform(basedep));
        last_menu = 0;
        for (m = parent->next; m; m = m->next) {
            dep = m->prompt ? m->prompt->visible.expr : m->dep;
            if (!expr_contains_symbol(dep, s))
                break;
            if (!expr_depends_symbol(dep, s)) {
                dep = expr_trans_compare(dep, E_UNEQUAL, &symbol_no);
                dep = expr_eliminate_dups(expr_transform(dep));
                dep2 = expr_copy(basedep);
                expr_eliminate_eq(&dep, &dep2);
                if (!expr_is_yes(dep2))
                    break;
            }
            menu_finalize(m);
            m->parent = parent;
            last_menu = m;
        }
        if (last_menu) {
            parent->list = parent->next;
            parent->next = last_menu->next;
            last_menu->next = 0;
        }
        s->dir_dep.expr = expr_alloc_or(s->dir_dep.expr, parent->dep);
    }
    for (m = parent->list; m; m = m->next) {
        if (s && sym_is_choice(s) && m->sym && !sym_is_choice_value(m->sym)) {
            current_entry = m;
            m->sym->flags = m->sym->flags | SYMBOL_CHOICEVAL;
            if (!m->prompt)
                menu_warn(m, "choice value must have a prompt%s", "");
            if (s->type == S_TRISTATE && m->sym->type != S_TRISTATE) {
                basedep = expr_alloc_comp(E_EQUAL, s, &symbol_yes);
                m->dep = expr_alloc_and(basedep, m->dep);
                for (p = m->sym->prop; p; p = p->next) {
                    if (p->menu != m)
                        continue;
                    p->visible.expr = expr_alloc_and(expr_copy(basedep), p->visible.expr);
                }
            }
            menu_add_symbol(P_CHOICE, s, 0);
            p = sym_get_choice_prop(s);
            for (ep = &p->expr; *ep; ep = &(*ep)->left.expr)
                ;
            *ep = expr_alloc_one(E_LIST, 0);
            (*ep)->right.sym = m->sym;
        }
        /* if の塊と，prompt の無い記号の下に作った副 menu を平らに戻す */
        if (m->list && (!m->prompt || !m->prompt->text)) {
            for (last_menu = m->list; ; last_menu = last_menu->next) {
                last_menu->parent = parent;
                if (!last_menu->next)
                    break;
            }
            last_menu->next = m->next;
            m->next = m->list;
            m->list = 0;
        }
    }
    if (s && !sym_is_optional(s) && parent->prompt) {
        s->rev_dep.expr = expr_alloc_or(s->rev_dep.expr,
            expr_alloc_and(parent->prompt->visible.expr, expr_alloc_symbol(&symbol_mod)));
    }
}

static int menu_is_visible(struct menu *m)
{
    struct menu *child;
    struct sym *s;
    int visible;
    if (!m->prompt)
        return 0;
    if (m->visibility) {
        if (expr_calc_value(m->visibility) == no)
            return 0;
    }
    s = m->sym;
    if (s) {
        sym_calc_value(s);
        visible = m->prompt->visible.tri;
    } else {
        visible = expr_calc_value(m->prompt->visible.expr);
        m->prompt->visible.tri = visible;
    }
    if (visible != no)
        return 1;
    if (!s || sym_get_tristate_value(m->sym) == no)
        return 0;
    for (child = m->list; child; child = child->next) {
        if (menu_is_visible(child)) {
            if (s)
                s->flags = s->flags | SYMBOL_DEF_USER;
            return 1;
        }
    }
    return 0;
}

static const char *menu_get_prompt(struct menu *m)
{
    if (m->prompt)
        return m->prompt->text;
    else if (m->sym)
        return m->sym->name;
    return 0;
}

/* ---- 構文解析 ---- */

static int la = -1;

static void parse_error(const char *msg)
{
    die("%s:%d: %s", current_file ? current_file->name : "<none>", stmt_lineno, msg);
}

static int peek(void)
{
    if (la < 0)
        la = yylex();
    return la;
}

static void consume(void)
{
    la = -1;
}

static void expect(int t, const char *msg)
{
    if (peek() != t)
        parse_error(msg);
    consume();
}

static char *take(void)
{
    char *v;
    v = tokval;
    consume();
    return v;
}

static struct expr *parse_expr(void);

static struct sym *parse_symbol(void)
{
    int t;
    t = peek();
    if (t == T_WORD)
        return sym_lookup(take(), 0);
    if (t == T_WORD_QUOTE)
        return sym_lookup(take(), SYMBOL_CONST);
    parse_error("symbol expected");
    return 0;
}

static struct expr *parse_primary(void)
{
    struct expr *e;
    struct sym *s1;
    struct sym *s2;
    int type;
    if (peek() == T_OPEN_PAREN) {
        consume();
        e = parse_expr();
        expect(T_CLOSE_PAREN, "')' expected");
        return e;
    }
    s1 = parse_symbol();
    switch (peek()) {
    case T_LESS: type = E_LTH; break;
    case T_LESS_EQUAL: type = E_LEQ; break;
    case T_GREATER: type = E_GTH; break;
    case T_GREATER_EQUAL: type = E_GEQ; break;
    case T_EQUAL: type = E_EQUAL; break;
    case T_UNEQUAL: type = E_UNEQUAL; break;
    default:
        return expr_alloc_symbol(s1);
    }
    consume();
    s2 = parse_symbol();
    return expr_alloc_comp(type, s1, s2);
}

static struct expr *parse_not(void)
{
    if (peek() == T_NOT) {
        consume();
        return expr_alloc_one(E_NOT, parse_not());
    }
    return parse_primary();
}

static struct expr *parse_and(void)
{
    struct expr *e;
    e = parse_not();
    while (peek() == T_AND) {
        consume();
        e = expr_alloc_two(E_AND, e, parse_not());
    }
    return e;
}

static struct expr *parse_expr(void)
{
    struct expr *e;
    e = parse_and();
    while (peek() == T_OR) {
        consume();
        e = expr_alloc_two(E_OR, e, parse_and());
    }
    return e;
}

static struct expr *parse_if_expr(void)
{
    if (peek() == T_IF) {
        consume();
        return parse_expr();
    }
    return 0;
}

static char *parse_prompt(void)
{
    int t;
    t = peek();
    if (t == T_WORD || t == T_WORD_QUOTE)
        return take();
    parse_error("prompt expected");
    return 0;
}

static int is_prompt_token(int t)
{
    return t == T_WORD || t == T_WORD_QUOTE;
}

static void parse_help(void)
{
    consume();
    if (peek() != T_EOL)
        parse_error("end of line expected after help");
    consume();
    lex_mode = L_HELP;
    lex_help_reset();
    if (peek() != T_HELPTEXT)
        parse_error("help text expected");
    consume();
    current_entry->has_help = 1;
}

static void parse_depends(void)
{
    consume();
    expect(T_ON, "'on' expected");
    menu_add_dep(parse_expr());
    expect(T_EOL, "end of line expected");
}

static int type_of(int t)
{
    switch (t) {
    case T_BOOL: return S_BOOLEAN;
    case T_TRISTATE: return S_TRISTATE;
    case T_INT: return S_INT;
    case T_HEX: return S_HEX;
    case T_STRING: return S_STRING;
    }
    return S_UNKNOWN;
}

/* config / menuconfig の属性の並び */
static void parse_config_options(void)
{
    int t;
    int type;
    char *text;
    struct expr *e;
    struct expr *dep;
    struct sym *s1;
    struct sym *s2;
    for (;;) {
        t = peek();
        switch (t) {
        case T_BOOL:
        case T_TRISTATE:
        case T_INT:
        case T_HEX:
        case T_STRING:
            consume();
            type = type_of(t);
            if (is_prompt_token(peek())) {
                text = take();
                menu_add_prompt(P_PROMPT, text, parse_if_expr());
            }
            expect(T_EOL, "end of line expected");
            menu_set_type(type);
            break;
        case T_PROMPT:
            consume();
            text = parse_prompt();
            menu_add_prompt(P_PROMPT, text, parse_if_expr());
            expect(T_EOL, "end of line expected");
            break;
        case T_DEFAULT:
        case T_DEF_BOOL:
        case T_DEF_TRISTATE:
            consume();
            e = parse_expr();
            dep = parse_if_expr();
            expect(T_EOL, "end of line expected");
            menu_add_expr(P_DEFAULT, e, dep);
            if (t == T_DEF_BOOL)
                menu_set_type(S_BOOLEAN);
            else if (t == T_DEF_TRISTATE)
                menu_set_type(S_TRISTATE);
            break;
        case T_SELECT:
        case T_IMPLY:
            consume();
            if (peek() != T_WORD)
                parse_error("symbol expected");
            s1 = sym_lookup(take(), 0);
            dep = parse_if_expr();
            expect(T_EOL, "end of line expected");
            menu_add_symbol(t == T_SELECT ? P_SELECT : P_IMPLY, s1, dep);
            break;
        case T_RANGE:
            consume();
            s1 = parse_symbol();
            s2 = parse_symbol();
            dep = parse_if_expr();
            expect(T_EOL, "end of line expected");
            menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE, s1, s2), dep);
            break;
        case T_OPTION:
            consume();
            t = peek();
            consume();
            if (t == T_MODULES) {
                if (modules_sym)
                    parse_error("option 'modules' already defined");
                modules_sym = current_entry->sym;
            } else if (t == T_DEFCONFIG_LIST) {
                if (!sym_defconfig_list)
                    sym_defconfig_list = current_entry->sym;
                else if (sym_defconfig_list != current_entry->sym)
                    parse_error("trying to redefine defconfig symbol");
                sym_defconfig_list->flags = sym_defconfig_list->flags | SYMBOL_NO_WRITE;
            } else if (t == T_ALLNOCONFIG_Y) {
                current_entry->sym->flags = current_entry->sym->flags | SYMBOL_ALLNOCONFIG_Y;
            } else {
                parse_error("unknown option");
            }
            expect(T_EOL, "end of line expected");
            break;
        case T_DEPENDS:
            parse_depends();
            break;
        case T_HELP:
            parse_help();
            break;
        default:
            return;
        }
    }
}

static void parse_choice_options(void)
{
    int t;
    char *text;
    struct expr *dep;
    struct sym *s1;
    for (;;) {
        t = peek();
        switch (t) {
        case T_PROMPT:
            consume();
            text = parse_prompt();
            menu_add_prompt(P_PROMPT, text, parse_if_expr());
            expect(T_EOL, "end of line expected");
            break;
        case T_BOOL:
        case T_TRISTATE:
            consume();
            if (is_prompt_token(peek())) {
                text = take();
                menu_add_prompt(P_PROMPT, text, parse_if_expr());
            }
            expect(T_EOL, "end of line expected");
            menu_set_type(type_of(t));
            break;
        case T_OPTIONAL:
            consume();
            expect(T_EOL, "end of line expected");
            current_entry->sym->flags = current_entry->sym->flags | SYMBOL_OPTIONAL;
            break;
        case T_DEFAULT:
            consume();
            if (peek() != T_WORD)
                parse_error("symbol expected");
            s1 = sym_lookup(take(), 0);
            dep = parse_if_expr();
            expect(T_EOL, "end of line expected");
            menu_add_symbol(P_DEFAULT, s1, dep);
            break;
        case T_DEPENDS:
            parse_depends();
            break;
        case T_HELP:
            parse_help();
            break;
        default:
            return;
        }
    }
}

/* end の字句と，対になる開始が同じファイルにあることを確かめる */
static void parse_end(int token, const char *name)
{
    if (peek() != token)
        parse_error("unexpected end of block");
    consume();
    expect(T_EOL, "end of line expected");
    if (current_menu->file != current_file) {
        fprintf(stderr, "'end%s' in different file than '%s'\n", name, name);
        parse_error("block ends in a different file");
    }
    menu_end_menu();
}

#define CTX_TOP    0
#define CTX_CHOICE 1

static void parse_stmt_list(int ctx);

static void parse_if(void)
{
    struct expr *e;
    consume();
    e = parse_expr();
    expect(T_EOL, "end of line expected");
    menu_add_entry(0);
    menu_add_dep(e);
    menu_add_menu();
    parse_stmt_list(CTX_TOP);
    parse_end(T_ENDIF, "if");
}

static void parse_config(int menuconfig)
{
    struct sym *s;
    consume();
    if (peek() != T_WORD)
        parse_error("symbol name expected");
    s = sym_lookup(take(), 0);
    expect(T_EOL, "end of line expected");
    s->flags = s->flags | SYMBOL_OPTIONAL;
    menu_add_entry(s);
    parse_config_options();
    if (menuconfig) {
        if (current_entry->prompt)
            current_entry->prompt->type = P_MENU;
        else
            fprintf(stderr, "%s:%d:warning: menuconfig statement without prompt\n",
                    current_file->name, stmt_lineno);
    }
}

static void parse_choice(void)
{
    char *name;
    struct sym *s;
    consume();
    name = 0;
    if (peek() == T_WORD)
        name = take();
    expect(T_EOL, "end of line expected");
    s = sym_lookup(name, SYMBOL_CHOICE);
    s->flags = s->flags | SYMBOL_NO_WRITE;
    menu_add_entry(s);
    menu_add_expr(P_CHOICE, 0, 0);
    parse_choice_options();
    menu_add_menu();
    parse_stmt_list(CTX_CHOICE);
    parse_end(T_ENDCHOICE, "choice");
}

static void parse_menu(void)
{
    char *text;
    int t;
    consume();
    text = parse_prompt();
    expect(T_EOL, "end of line expected");
    menu_add_entry(0);
    menu_add_prompt(P_MENU, text, 0);
    for (;;) {
        t = peek();
        if (t == T_VISIBLE) {
            consume();
            menu_add_visibility(parse_if_expr());
            expect(T_EOL, "end of line expected");
        } else if (t == T_DEPENDS) {
            parse_depends();
        } else
            break;
    }
    menu_add_menu();
    parse_stmt_list(CTX_TOP);
    parse_end(T_ENDMENU, "menu");
}

static void parse_source(void)
{
    char *name;
    consume();
    name = parse_prompt();
    if (peek() != T_EOL)
        parse_error("end of line expected");
    consume();
    zconf_nextfile(name);
}

static void parse_comment(void)
{
    char *text;
    consume();
    text = parse_prompt();
    expect(T_EOL, "end of line expected");
    menu_add_entry(0);
    menu_add_prompt(P_COMMENT, text, 0);
    while (peek() == T_DEPENDS)
        parse_depends();
}

static void parse_assignment(void)
{
    char *name;
    char *val;
    int t;
    int flavor;
    name = take();
    t = peek();
    if (t == T_EQUAL)
        flavor = VAR_RECURSIVE;
    else if (t == T_COLON_EQUAL)
        flavor = VAR_SIMPLE;
    else if (t == T_PLUS_EQUAL)
        flavor = VAR_APPEND;
    else {
        parse_error("unknown statement");
        return;
    }
    consume();
    val = "";
    if (peek() == T_ASSIGN_VAL)
        val = take();
    expect(T_EOL, "end of line expected");
    variable_add(name, val, flavor);
}

/* 文の並び。choice の中には choice と menu を置けない */
static void parse_stmt_list(int ctx)
{
    int t;
    for (;;) {
        t = peek();
        switch (t) {
        case T_EOF:
        case T_ENDMENU:
        case T_ENDCHOICE:
        case T_ENDIF:
            return;
        case T_IF:
            parse_if();
            break;
        case T_COMMENT:
            parse_comment();
            break;
        case T_CONFIG:
            parse_config(0);
            break;
        case T_MENUCONFIG:
            parse_config(1);
            break;
        case T_SOURCE:
            parse_source();
            break;
        case T_WORD:
            parse_assignment();
            break;
        case T_CHOICE:
            if (ctx == CTX_CHOICE)
                parse_error("choice inside a choice block");
            parse_choice();
            break;
        case T_MENU:
            if (ctx == CTX_CHOICE)
                parse_error("menu inside a choice block");
            parse_menu();
            break;
        default:
            parse_error("invalid statement");
        }
    }
}

static void conf_parse(const char *name)
{
    char *text;
    zconf_initscan(name);
    current_entry = &rootmenu;
    current_menu = &rootmenu;
    last_entry_ptr = &rootmenu.list;
    if (peek() == T_MAINMENU) {
        consume();
        text = parse_prompt();
        expect(T_EOL, "end of line expected");
        menu_add_prompt(P_MENU, text, 0);
    }
    parse_stmt_list(CTX_TOP);
    if (peek() != T_EOF)
        parse_error("unexpected end of block");
    if (!modules_sym)
        modules_sym = sym_find("n");
    if (!rootmenu.prompt) {
        current_entry = &rootmenu;
        menu_add_prompt(P_MENU, "Main menu", 0);
    }
    menu_finalize(&rootmenu);
    sym_set_change_count(1);
}

/* ---- 設定ファイルの読み書き ---- */

static const char *conf_filename;
static int conf_lineno;
static int conf_warnings;

static void conf_warning(const char *fmt, const char *arg)
{
    fprintf(stderr, "%s:%d:warning: ", conf_filename, conf_lineno);
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    conf_warnings = conf_warnings + 1;
}

static void conf_message(const char *fmt, const char *arg)
{
    if (silent)
        return;
    printf("#\n# ");
    printf(fmt, arg);
    printf("\n#\n");
}

static const char *conf_get_configname(void)
{
    char *name;
    name = getenv("KCONFIG_CONFIG");
    return name ? name : ".config";
}

static const char *conf_get_autoconfig_name(void)
{
    char *name;
    name = getenv("KCONFIG_AUTOCONFIG");
    return name ? name : "include/config/auto.conf";
}

static int is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st))
        return 0;
    return S_ISDIR(st.st_mode);
}

/* path の親ディレクトリを作る (include/config/auto.conf なら include/config) */
static int make_parent_dir(const char *path)
{
    char *tmp;
    char *p;
    tmp = xstrdup(path);
    p = strrchr(tmp, '/');
    if (!p)
        return 0;
    p[1] = 0;
    p = tmp;
    while (*p == '/')
        p++;
    while ((p = strchr(p, '/')) != 0) {
        *p = 0;
        if (!is_dir(tmp) && mkdir(tmp, 0755))
            return -1;
        *p = '/';
        while (*p == '/')
            p++;
    }
    return 0;
}

/* 2 つのファイルが同じ中身か */
static int is_same(const char *f1, const char *f2)
{
    FILE *a;
    FILE *b;
    int c1;
    int c2;
    a = fopen(f1, "r");
    if (!a)
        return 0;
    b = fopen(f2, "r");
    if (!b) {
        fclose(a);
        return 0;
    }
    for (;;) {
        c1 = getc(a);
        c2 = getc(b);
        if (c1 != c2 || c1 == EOF)
            break;
    }
    fclose(a);
    fclose(b);
    return c1 == c2;
}

/* 1 行を読む。改行を含めて返す。読めなければ 0 */
static char *read_line(FILE *f)
{
    struct sbuf b;
    int c;
    char ch;
    sb_init(&b);
    for (;;) {
        c = getc(f);
        if (c == EOF)
            break;
        ch = (char)c;
        sb_addn(&b, &ch, 1);
        if (c == '\n')
            break;
    }
    if (b.len == 0)
        return 0;
    return b.s;
}

static int conf_set_sym_val(struct sym *s, int def, int def_flags, char *p)
{
    char *p2;
    switch (s->type) {
    case S_TRISTATE:
        if (p[0] == 'm') {
            s->def[def].tri = mod;
            s->flags = s->flags | def_flags;
            break;
        }
        /* fall through */
    case S_BOOLEAN:
        if (p[0] == 'y') {
            s->def[def].tri = yes;
            s->flags = s->flags | def_flags;
            break;
        }
        if (p[0] == 'n') {
            s->def[def].tri = no;
            s->flags = s->flags | def_flags;
            break;
        }
        if (def != S_DEF_AUTO)
            conf_warning("symbol value '%s' invalid", p);
        return 1;
    case S_STRING:
        if (*p++ != '"')
            break;
        for (p2 = p; (p2 = strpbrk(p2, "\"\\")) != 0; p2++) {
            if (*p2 == '"') {
                *p2 = 0;
                break;
            }
            memmove(p2, p2 + 1, strlen(p2));
        }
        if (!p2) {
            if (def != S_DEF_AUTO)
                conf_warning("invalid string found%s", "");
            return 1;
        }
        /* fall through */
    case S_INT:
    case S_HEX:
        if (sym_string_valid(s, p)) {
            s->def[def].val = xstrdup(p);
            s->flags = s->flags | def_flags;
        } else {
            if (def != S_DEF_AUTO)
                conf_warning("symbol value '%s' invalid", p);
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

static char depfile_path[4096];
static size_t depfile_prefix_len;

/* 記号ごとの依存ファイル include/config/<名前>.h を作る (fixdep が使う) */
static int conf_touch_dep(const char *name)
{
    int fd;
    const char *s;
    char *d;
    int c;
    if (depfile_prefix_len + strlen(name) + 3 > sizeof(depfile_path))
        return -1;
    d = depfile_path + depfile_prefix_len;
    for (s = name; (c = (unsigned char)*s) != 0; s++)
        *d++ = (c == '_') ? '/' : (char)tolower(c);
    strcpy(d, ".h");
    fd = open(depfile_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        if (make_parent_dir(depfile_path))
            return -1;
        fd = open(depfile_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
            return -1;
    }
    close(fd);
    return 0;
}

#define CONFIG_ "CONFIG_"

static int conf_read_simple(const char *name, int def)
{
    FILE *in;
    char *line;
    char *p;
    char *p2;
    struct sym *s;
    struct sym *cs;
    struct prop *pr;
    int i;
    int def_flags;
    size_t cl;
    in = 0;
    if (name) {
        in = zconf_fopen(name);
    } else {
        name = conf_get_configname();
        in = zconf_fopen(name);
        if (!in) {
            sym_add_change_count(1);
            if (!sym_defconfig_list)
                return 1;
            for (pr = sym_defconfig_list->prop; pr; pr = pr->next) {
                if (pr->type != P_DEFAULT)
                    continue;
                if (expr_calc_value(pr->visible.expr) == no ||
                    pr->expr->type != E_SYMBOL)
                    continue;
                sym_calc_value(pr->expr->left.sym);
                name = sym_get_string_value(pr->expr->left.sym);
                in = zconf_fopen(name);
                if (in) {
                    conf_message("using defaults found in %s", name);
                    break;
                }
            }
        }
    }
    if (!in)
        return 1;
    conf_filename = name;
    conf_lineno = 0;
    conf_warnings = 0;
    def_flags = SYMBOL_DEF << def;
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            s->flags = s->flags | SYMBOL_CHANGED;
            s->flags = s->flags & ~(def_flags | SYMBOL_VALID);
            if (sym_is_choice(s))
                s->flags = s->flags | def_flags;
            s->def[def].val = 0;
            s->def[def].tri = no;
        }
    }
    cl = strlen(CONFIG_);
    while ((line = read_line(in)) != 0) {
        conf_lineno = conf_lineno + 1;
        s = 0;
        if (line[0] == '#') {
            if (strlen(line) < 2 + cl || memcmp(line + 2, CONFIG_, cl))
                continue;
            p = strchr(line + 2 + cl, ' ');
            if (!p)
                continue;
            *p++ = 0;
            if (strncmp(p, "is not set", 10))
                continue;
            if (def == S_DEF_USER) {
                s = sym_find(line + 2 + cl);
                if (!s) {
                    sym_add_change_count(1);
                    continue;
                }
            } else {
                s = sym_lookup(line + 2 + cl, 0);
                if (s->type == S_UNKNOWN)
                    s->type = S_BOOLEAN;
            }
            if (s->flags & def_flags)
                conf_warning("override: reassigning to symbol %s", s->name);
            switch (s->type) {
            case S_BOOLEAN:
            case S_TRISTATE:
                s->def[def].tri = no;
                s->flags = s->flags | def_flags;
                break;
            default:
                break;
            }
        } else if (memcmp(line, CONFIG_, cl) == 0) {
            p = strchr(line + cl, '=');
            if (!p)
                continue;
            *p++ = 0;
            p2 = strchr(p, '\n');
            if (p2) {
                *p2-- = 0;
                if (*p2 == '\r')
                    *p2 = 0;
            }
            s = sym_find(line + cl);
            if (!s) {
                if (def == S_DEF_AUTO)
                    conf_touch_dep(line + cl);
                else
                    sym_add_change_count(1);
                continue;
            }
            if (s->flags & def_flags)
                conf_warning("override: reassigning to symbol %s", s->name);
            if (conf_set_sym_val(s, def, def_flags, p))
                continue;
        } else {
            if (line[0] != '\r' && line[0] != '\n')
                conf_warning("unexpected data: %s", line);
            continue;
        }
        if (s && sym_is_choice_value(s)) {
            cs = prop_get_symbol(sym_get_choice_prop(s));
            switch (s->def[def].tri) {
            case no:
                break;
            case mod:
                if (cs->def[def].tri == yes) {
                    conf_warning("%s creates inconsistent choice state", s->name);
                    cs->flags = cs->flags & ~def_flags;
                }
                break;
            case yes:
                if (cs->def[def].tri != no)
                    conf_warning("override: %s changes choice state", s->name);
                cs->def[def].val = s;
                break;
            }
            cs->def[def].tri = EXPR_OR(cs->def[def].tri, s->def[def].tri);
        }
    }
    fclose(in);
    return 0;
}

static int conf_read(const char *name)
{
    struct sym *s;
    int conf_unsaved;
    int i;
    conf_unsaved = 0;
    sym_set_change_count(0);
    if (conf_read_simple(name, S_DEF_USER)) {
        sym_calc_value(modules_sym);
        return 1;
    }
    sym_calc_value(modules_sym);
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            sym_calc_value(s);
            if (sym_is_choice(s) || (s->flags & SYMBOL_NO_WRITE))
                continue;
            if (sym_has_value(s) && (s->flags & SYMBOL_WRITE)) {
                switch (s->type) {
                case S_BOOLEAN:
                case S_TRISTATE:
                    if (s->def[S_DEF_USER].tri == sym_get_tristate_value(s))
                        continue;
                    break;
                default:
                    if (!strcmp(s->curr.val, s->def[S_DEF_USER].val))
                        continue;
                    break;
                }
            } else if (!sym_has_value(s) && !(s->flags & SYMBOL_WRITE))
                continue;
            conf_unsaved = conf_unsaved + 1;
        }
    }
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            if (sym_has_value(s) && !sym_is_choice_value(s)) {
                if (s->visible == no && !conf_unsaved)
                    s->flags = s->flags & ~SYMBOL_DEF_USER;
                switch (s->type) {
                case S_STRING:
                case S_INT:
                case S_HEX:
                    if (sym_string_within_range(s, s->def[S_DEF_USER].val))
                        break;
                    s->flags = s->flags & ~(SYMBOL_VALID | SYMBOL_DEF_USER);
                    conf_unsaved = conf_unsaved + 1;
                    break;
                default:
                    break;
                }
            }
        }
    }
    sym_add_change_count(conf_warnings || conf_unsaved);
    return 0;
}

/* 出力の形。.config / auto.conf / autoconf.h / tristate.conf */
#define PR_KCONFIG  0
#define PR_HEADER   1
#define PR_TRISTATE 2

static void print_symbol(FILE *fp, int printer, struct sym *s, const char *value, int skip_unset)
{
    const char *suffix;
    const char *prefix;
    switch (printer) {
    case PR_KCONFIG:
        if (s->type == S_BOOLEAN || s->type == S_TRISTATE) {
            if (*value == 'n') {
                if (!skip_unset)
                    fprintf(fp, "# %s%s is not set\n", CONFIG_, s->name);
                return;
            }
        }
        fprintf(fp, "%s%s=%s\n", CONFIG_, s->name, value);
        break;
    case PR_HEADER:
        switch (s->type) {
        case S_BOOLEAN:
        case S_TRISTATE:
            suffix = "";
            switch (*value) {
            case 'n':
                break;
            case 'm':
                suffix = "_MODULE";
                fprintf(fp, "#define %s%s%s 1\n", CONFIG_, s->name, suffix);
                break;
            default:
                fprintf(fp, "#define %s%s%s 1\n", CONFIG_, s->name, suffix);
                break;
            }
            break;
        case S_HEX:
            prefix = "";
            if (value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
                prefix = "0x";
            fprintf(fp, "#define %s%s %s%s\n", CONFIG_, s->name, prefix, value);
            break;
        case S_STRING:
        case S_INT:
            fprintf(fp, "#define %s%s %s\n", CONFIG_, s->name, value);
            break;
        default:
            break;
        }
        break;
    case PR_TRISTATE:
        if (s->type == S_TRISTATE && *value != 'n')
            fprintf(fp, "%s%s=%c\n", CONFIG_, s->name, (char)toupper((unsigned char)*value));
        break;
    }
}

static void print_comment(FILE *fp, int printer, const char *value)
{
    const char *p;
    size_t l;
    p = value;
    if (printer == PR_HEADER)
        fprintf(fp, "/*\n");
    for (;;) {
        l = strcspn(p, "\n");
        fprintf(fp, printer == PR_HEADER ? " *" : "#");
        if (l) {
            fprintf(fp, " ");
            fwrite(p, l, 1, fp);
            p = p + l;
        }
        fprintf(fp, "\n");
        if (*p++ == '\0')
            break;
    }
    if (printer == PR_HEADER)
        fprintf(fp, " */\n");
}

static void conf_write_symbol(FILE *fp, struct sym *s, int printer, int skip_unset)
{
    const char *str;
    char *esc;
    switch (s->type) {
    case S_UNKNOWN:
        break;
    case S_STRING:
        str = sym_get_string_value(s);
        esc = sym_escape_string_value(str);
        print_symbol(fp, printer, s, esc, skip_unset);
        break;
    default:
        str = sym_get_string_value(s);
        print_symbol(fp, printer, s, str, skip_unset);
        break;
    }
}

static void conf_write_heading(FILE *fp, int printer)
{
    struct sbuf b;
    sb_init(&b);
    sb_add(&b, "\nAutomatically generated file; DO NOT EDIT.\n");
    sb_add(&b, rootmenu.prompt->text);
    sb_add(&b, "\n");
    print_comment(fp, printer, b.s);
}

/* menu の木を深さ優先で次へ進む */
static struct menu *menu_next_dfs(struct menu *m)
{
    if (m->list)
        return m->list;
    if (m->next)
        return m->next;
    while ((m = m->parent) != 0) {
        if (m->next)
            return m->next;
    }
    return 0;
}

static int conf_write_defconfig(const char *filename)
{
    struct sym *s;
    struct sym *cs;
    struct sym *ds;
    struct menu *m;
    FILE *out;
    out = fopen(filename, "w");
    if (!out)
        return 1;
    sym_clear_all_valid();
    for (m = rootmenu.list; m; m = menu_next_dfs(m)) {
        s = m->sym;
        if (s == 0) {
            if (!menu_is_visible(m))
                continue;
        } else if (!sym_is_choice(s)) {
            sym_calc_value(s);
            if (!(s->flags & SYMBOL_WRITE))
                continue;
            s->flags = s->flags & ~SYMBOL_WRITE;
            if (!sym_is_changeable(s))
                continue;
            if (strcmp(sym_get_string_value(s), sym_get_string_default(s)) == 0)
                continue;
            if (sym_is_choice_value(s)) {
                cs = prop_get_symbol(sym_get_choice_prop(s));
                ds = sym_choice_default(cs);
                if (!sym_is_optional(cs) && s == ds) {
                    if (s->type == S_BOOLEAN && sym_get_tristate_value(s) == yes)
                        continue;
                }
            }
            conf_write_symbol(out, s, PR_KCONFIG, 0);
        }
    }
    fclose(out);
    return 0;
}

static int conf_write(const char *name)
{
    FILE *out;
    struct sym *s;
    struct menu *m;
    const char *str;
    char tmpname[4096];
    char oldname[4096];
    char *env;
    int i;
    int need_newline;
    need_newline = 0;
    if (!name)
        name = conf_get_configname();
    if (!*name)
        die("config name is empty");
    if (is_dir(name))
        die("%s: Is a directory", name);
    if (make_parent_dir(name))
        return -1;
    env = getenv("KCONFIG_OVERWRITECONFIG");
    if (env && *env) {
        tmpname[0] = 0;
        out = fopen(name, "w");
    } else {
        sprintf(tmpname, "%s.%d.tmp", name, (int)getpid());
        out = fopen(tmpname, "w");
    }
    if (!out)
        return 1;
    conf_write_heading(out, PR_KCONFIG);
    if (!sym_change_count)
        sym_clear_all_valid();
    m = rootmenu.list;
    while (m) {
        s = m->sym;
        if (!s) {
            if (menu_is_visible(m)) {
                str = menu_get_prompt(m);
                fprintf(out, "\n#\n# %s\n#\n", str);
                need_newline = 0;
            }
        } else if (!(s->flags & SYMBOL_CHOICE) && !(s->flags & SYMBOL_WRITTEN)) {
            sym_calc_value(s);
            if (s->flags & SYMBOL_WRITE) {
                if (need_newline) {
                    fprintf(out, "\n");
                    need_newline = 0;
                }
                s->flags = s->flags | SYMBOL_WRITTEN;
                conf_write_symbol(out, s, PR_KCONFIG, 0);
            }
        }
        if (m->list) {
            m = m->list;
            continue;
        }
        if (m->next)
            m = m->next;
        else {
            while ((m = m->parent) != 0) {
                if (!m->sym && menu_is_visible(m) && m != &rootmenu) {
                    str = menu_get_prompt(m);
                    fprintf(out, "# end of %s\n", str);
                    need_newline = 1;
                }
                if (m->next) {
                    m = m->next;
                    break;
                }
            }
        }
    }
    fclose(out);
    for (i = 0; i < SYMBOL_HASHSIZE; i++)
        for (s = symbol_hash[i]; s; s = s->next)
            s->flags = s->flags & ~SYMBOL_WRITTEN;
    if (tmpname[0]) {
        if (is_same(name, tmpname)) {
            conf_message("No change to %s", name);
            unlink(tmpname);
            sym_set_change_count(0);
            return 0;
        }
        sprintf(oldname, "%s.old", name);
        rename(name, oldname);
        if (rename(tmpname, name))
            return 1;
    }
    conf_message("configuration written to %s", name);
    sym_set_change_count(0);
    return 0;
}

/* include/config/auto.conf.cmd: Kbuild が auto.conf の作り直しを判断する */
static int conf_write_dep(const char *name)
{
    struct kfile *f;
    struct env *e;
    FILE *out;
    const char *ac;
    out = fopen("..config.tmp", "w");
    if (!out)
        return 1;
    fprintf(out, "deps_config := \\\n");
    for (f = file_list; f; f = f->next) {
        if (f->next)
            fprintf(out, "\t%s \\\n", f->name);
        else
            fprintf(out, "\t%s\n", f->name);
    }
    ac = conf_get_autoconfig_name();
    fprintf(out, "\n%s: \\\n\t$(deps_config)\n\n", ac);
    for (e = env_list; e; e = e->next) {
        fprintf(out, "ifneq \"$(%s)\" \"%s\"\n", e->name, e->value);
        fprintf(out, "%s: FORCE\n", ac);
        fprintf(out, "endif\n");
    }
    env_list = 0;
    env_tail = &env_list;
    fprintf(out, "\n$(deps_config): ;\n");
    fclose(out);
    if (make_parent_dir(name))
        return 1;
    rename("..config.tmp", name);
    return 0;
}

static int conf_touch_deps(void)
{
    const char *name;
    const char *tmp;
    struct sym *s;
    int res;
    int i;
    name = conf_get_autoconfig_name();
    tmp = strrchr(name, '/');
    depfile_prefix_len = tmp ? (size_t)(tmp - name + 1) : 0;
    if (depfile_prefix_len + 1 > sizeof(depfile_path))
        return -1;
    memcpy(depfile_path, name, depfile_prefix_len);
    depfile_path[depfile_prefix_len] = 0;
    conf_read_simple(name, S_DEF_AUTO);
    sym_calc_value(modules_sym);
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            sym_calc_value(s);
            if ((s->flags & SYMBOL_NO_WRITE) || !s->name)
                continue;
            if (s->flags & SYMBOL_WRITE) {
                if (s->flags & SYMBOL_DEF_AUTO) {
                    switch (s->type) {
                    case S_BOOLEAN:
                    case S_TRISTATE:
                        if (sym_get_tristate_value(s) == s->def[S_DEF_AUTO].tri)
                            continue;
                        break;
                    case S_STRING:
                    case S_HEX:
                    case S_INT:
                        if (!strcmp(sym_get_string_value(s), s->def[S_DEF_AUTO].val))
                            continue;
                        break;
                    default:
                        break;
                    }
                } else {
                    switch (s->type) {
                    case S_BOOLEAN:
                    case S_TRISTATE:
                        if (sym_get_tristate_value(s) == no)
                            continue;
                        break;
                    default:
                        break;
                    }
                }
            } else if (!(s->flags & SYMBOL_DEF_AUTO))
                continue;
            res = conf_touch_dep(s->name);
            if (res)
                return res;
        }
    }
    return 0;
}

static int conf_write_autoconf(int overwrite)
{
    struct sym *s;
    const char *name;
    const char *autoconf_name;
    FILE *out;
    FILE *tristate;
    FILE *out_h;
    struct stat st;
    int i;
    autoconf_name = conf_get_autoconfig_name();
    if (!overwrite && !stat(autoconf_name, &st))
        return 0;
    conf_write_dep("include/config/auto.conf.cmd");
    if (conf_touch_deps())
        return 1;
    out = fopen(".tmpconfig", "w");
    if (!out)
        return 1;
    tristate = fopen(".tmpconfig_tristate", "w");
    if (!tristate) {
        fclose(out);
        return 1;
    }
    out_h = fopen(".tmpconfig.h", "w");
    if (!out_h) {
        fclose(out);
        fclose(tristate);
        return 1;
    }
    conf_write_heading(out, PR_KCONFIG);
    conf_write_heading(tristate, PR_TRISTATE);
    conf_write_heading(out_h, PR_HEADER);
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            sym_calc_value(s);
            if (!(s->flags & SYMBOL_WRITE) || !s->name)
                continue;
            conf_write_symbol(out, s, PR_KCONFIG, 1);
            conf_write_symbol(tristate, s, PR_TRISTATE, 1);
            conf_write_symbol(out_h, s, PR_HEADER, 0);
        }
    }
    fclose(out);
    fclose(tristate);
    fclose(out_h);
    name = getenv("KCONFIG_AUTOHEADER");
    if (!name)
        name = "include/generated/autoconf.h";
    if (make_parent_dir(name))
        return 1;
    if (rename(".tmpconfig.h", name))
        return 1;
    name = getenv("KCONFIG_TRISTATE");
    if (!name)
        name = "include/config/tristate.conf";
    if (make_parent_dir(name))
        return 1;
    if (rename(".tmpconfig_tristate", name))
        return 1;
    if (make_parent_dir(autoconf_name))
        return 1;
    if (rename(".tmpconfig", autoconf_name))
        return 1;
    return 0;
}

#define DEF_NO      0
#define DEF_YES     1
#define DEF_MOD     2
#define DEF_DEFAULT 3

static void conf_set_all_new_symbols(int mode)
{
    struct sym *s;
    struct sym *csym;
    int i;
    int t;
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (s = symbol_hash[i]; s; s = s->next) {
            if (sym_has_value(s) || (s->flags & SYMBOL_VALID))
                continue;
            t = sym_get_type(s);
            if (t != S_BOOLEAN && t != S_TRISTATE)
                continue;
            if (mode == DEF_DEFAULT)
                continue;
            switch (mode) {
            case DEF_YES:
                s->def[S_DEF_USER].tri = yes;
                break;
            case DEF_MOD:
                s->def[S_DEF_USER].tri = mod;
                break;
            case DEF_NO:
                if (s->flags & SYMBOL_ALLNOCONFIG_Y)
                    s->def[S_DEF_USER].tri = yes;
                else
                    s->def[S_DEF_USER].tri = no;
                break;
            }
            s->flags = s->flags | SYMBOL_DEF_USER;
        }
    }
    sym_clear_all_valid();
    for (i = 0; i < SYMBOL_HASHSIZE; i++)
        for (csym = symbol_hash[i]; csym; csym = csym->next)
            if ((sym_is_choice(csym) && !sym_has_value(csym)) || sym_is_choice_value(csym))
                csym->flags = csym->flags | SYMBOL_NEED_SET_CHOICE_VALUES;
    for (i = 0; i < SYMBOL_HASHSIZE; i++) {
        for (csym = symbol_hash[i]; csym; csym = csym->next) {
            if (sym_has_value(csym) || !sym_is_choice(csym))
                continue;
            sym_calc_value(csym);
            set_all_choice_values(csym);
        }
    }
}

/* syncconfig: 値の無い新しい記号が見える所にあるか */
static int has_new_symbols(struct menu *m)
{
    struct sym *s;
    struct menu *child;
    if (!menu_is_visible(m))
        return 0;
    s = m->sym;
    if (s && !sym_has_value(s)) {
        if (sym_is_changeable(s) ||
            (sym_is_choice(s) && sym_get_tristate_value(s) == yes))
            return 1;
    }
    for (child = m->list; child; child = child->next)
        if (has_new_symbols(child))
            return 1;
    return 0;
}

/* ---- main ---- */

#define M_NONE          0
#define M_ALLNO         1
#define M_ALLYES        2
#define M_ALLMOD        3
#define M_ALLDEF        4
#define M_OLDDEF        5
#define M_SYNC          6
#define M_DEFCONFIG     7
#define M_SAVEDEFCONFIG 8

static void usage(void)
{
    fprintf(stderr, "usage: kconf1 [-s] --{allnoconfig|allyesconfig|allmodconfig|alldefconfig|"
                    "olddefconfig|syncconfig|defconfig=<file>|savedefconfig=<file>} <Kconfig>\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int mode;
    int i;
    int sync_kconfig;
    const char *arg;
    const char *defconfig_file;
    const char *kconfig;
    const char *name;
    mode = M_NONE;
    sync_kconfig = 0;
    defconfig_file = 0;
    kconfig = 0;
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (!strcmp(arg, "-s")) {
            silent = 1;
        } else if (!strcmp(arg, "--allnoconfig")) {
            mode = M_ALLNO;
        } else if (!strcmp(arg, "--allyesconfig")) {
            mode = M_ALLYES;
        } else if (!strcmp(arg, "--allmodconfig")) {
            mode = M_ALLMOD;
        } else if (!strcmp(arg, "--alldefconfig")) {
            mode = M_ALLDEF;
        } else if (!strcmp(arg, "--olddefconfig")) {
            mode = M_OLDDEF;
        } else if (!strcmp(arg, "--syncconfig")) {
            mode = M_SYNC;
            silent = 1;
            sync_kconfig = 1;
        } else if (!strncmp(arg, "--defconfig=", 12)) {
            mode = M_DEFCONFIG;
            defconfig_file = arg + 12;
        } else if (!strcmp(arg, "--defconfig") && i + 1 < argc) {
            mode = M_DEFCONFIG;
            defconfig_file = argv[++i];
        } else if (!strncmp(arg, "--savedefconfig=", 16)) {
            mode = M_SAVEDEFCONFIG;
            defconfig_file = arg + 16;
        } else if (arg[0] == '-') {
            usage();
        } else {
            kconfig = arg;
        }
    }
    if (mode == M_NONE || !kconfig)
        usage();
    sym_init_consts();
    conf_parse(kconfig);
    switch (mode) {
    case M_DEFCONFIG:
        if (conf_read(defconfig_file)) {
            fprintf(stderr, "***\n*** Can't find default configuration \"%s\"!\n***\n",
                    defconfig_file);
            exit(1);
        }
        break;
    case M_SAVEDEFCONFIG:
    case M_SYNC:
    case M_OLDDEF:
        conf_read(0);
        break;
    case M_ALLNO:
    case M_ALLYES:
    case M_ALLMOD:
    case M_ALLDEF:
        name = getenv("KCONFIG_ALLCONFIG");
        if (!name)
            break;
        if (strcmp(name, "") != 0 && strcmp(name, "1") != 0) {
            if (conf_read_simple(name, S_DEF_USER)) {
                fprintf(stderr, "*** Can't read seed configuration \"%s\"!\n", name);
                exit(1);
            }
            break;
        }
        switch (mode) {
        case M_ALLNO: name = "allno.config"; break;
        case M_ALLYES: name = "allyes.config"; break;
        case M_ALLMOD: name = "allmod.config"; break;
        default: name = "alldef.config"; break;
        }
        if (conf_read_simple(name, S_DEF_USER) &&
            conf_read_simple("all.config", S_DEF_USER)) {
            fprintf(stderr, "*** KCONFIG_ALLCONFIG set, but no \"%s\" or \"all.config\" file found\n",
                    name);
            exit(1);
        }
        break;
    default:
        break;
    }
    switch (mode) {
    case M_ALLNO:
        conf_set_all_new_symbols(DEF_NO);
        break;
    case M_ALLYES:
        conf_set_all_new_symbols(DEF_YES);
        break;
    case M_ALLMOD:
        conf_set_all_new_symbols(DEF_MOD);
        break;
    case M_ALLDEF:
    case M_DEFCONFIG:
        conf_set_all_new_symbols(DEF_DEFAULT);
        break;
    case M_SYNC:
        if (has_new_symbols(&rootmenu))
            die("syncconfig: the configuration has new symbols; run olddefconfig first");
        break;
    default:
        break;
    }
    if (mode == M_SAVEDEFCONFIG) {
        if (conf_write_defconfig(defconfig_file)) {
            fprintf(stderr, "n*** Error while saving defconfig to: %s\n\n", defconfig_file);
            return 1;
        }
        return 0;
    }
    if (conf_write(0)) {
        fprintf(stderr, "\n*** Error during writing of the configuration.\n\n");
        exit(1);
    }
    if (conf_write_autoconf(sync_kconfig) && sync_kconfig) {
        fprintf(stderr, "\n*** Error during sync of the configuration.\n\n");
        return 1;
    }
    return 0;
}
