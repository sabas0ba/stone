/* ncc.c --- gcc の命令行を受けるドライバ (docs/stage019-linux.md 9 章)
 *
 * Linux 5.4 の Kbuild が CROSS_COMPILE の gcc として呼ぶ。命令行を読み，
 * 段ごとに我々の道具を起動する。
 *
 *   -E           npp1 (前処理だけ。-o が無ければ標準出力へ)
 *   -S  .c       npp1 → ncc1
 *   -c  .c       npp1 → ncc1 → nas1
 *   -c  .S       npp1 (assembler-with-cpp) → nas1
 *   -c  -x assembler     nas1 (前処理しない)
 *   それ以外      nld1 で結合する (-shared・-r・-T・-Wl,…)
 *
 * 問合せ (--version・-dumpversion・-print-file-name=…・-v) には GCC 4.6.4
 * として答える。npp1 と ncc1 が知らない選択肢 (-W*・-f*・-m* など) は
 * 読み捨てる。振舞いは tools/kb18cc.sh (ホストの sh で書いた同じ役目の
 * もの) と同じにしてある。
 *
 * 道具と処理系の資料の置き場:
 *   NCC_BIN      npp1・ncc1・nas1・nld1 の置き場        (既定 /lib/ncc)
 *   NCC_PREDEF   処理系が前もって定義するマクロ          (既定 /lib/ncc/predef.h)
 *   NCC_INCLUDE  処理系の include の階層 (stdarg.h など)  (既定 /lib/ncc/include)
 * 環境変数があればそれを使う。stone の OS は今は環境変数を渡さないので既定の
 * 置き場を使う。
 *
 * 中間の .i と .s は出力の名前に .ncc.i / .ncc.s を足した所に置き，終わり
 * に消す。出力が /dev/ の下にある時 (Kconfig の試しの -o /dev/null) と
 * 出力の無い時は，作業の場所に ncc-tmp-<番号> の名前で置く。
 *
 * C89 で書く。cc15as と libc26 で stone の OS 向けに組む (libc26 の spawn で
 * 子を起動する)。NCC_HOST を定義するとホストの POSIX (fork・execv・waitpid)
 * で組む。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NCC_HOST
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <unistd.h>
/* libc26 は getenv を持つ (常に 0 を返す) が stdlib.h に宣言が無い */
char *getenv(char *name);
#endif

#ifndef NCC_LIB
#define NCC_LIB "/lib/ncc"
#endif

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "ncc: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(1);
}

static void *xmalloc(long n)
{
    void *p = malloc((size_t)(n > 0 ? n : 1));
    if (!p)
        die("記憶域が足りない", 0);
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = xmalloc((long)strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static char *cat2(const char *a, const char *b)
{
    char *p = xmalloc((long)strlen(a) + (long)strlen(b) + 1);
    strcpy(p, a);
    strcat(p, b);
    return p;
}

/* ---- 文字列の並び (子に渡す argv) ---- */

struct vec {
    char **v;
    int n;
    int cap;
};

static void vadd(struct vec *a, const char *s)
{
    if (a->n + 2 > a->cap) {
        int i;
        char **nv;
        a->cap = a->cap ? a->cap * 2 : 32;
        nv = xmalloc((long)a->cap * (long)sizeof(char *));
        for (i = 0; i < a->n; i++)
            nv[i] = a->v[i];
        free(a->v);
        a->v = nv;
    }
    a->v[a->n++] = xstrdup(s);
    a->v[a->n] = 0;
}

static void vcat(struct vec *a, struct vec *b)
{
    int i;
    for (i = 0; i < b->n; i++)
        vadd(a, b->v[i]);
}

/* ---- 子の起動 ---- */

/* path を argv で起動して終わりを待ち，終了コードを返す。in / out は標準
 * 入出力を結ぶファイルの名前 (0 なら親のものを継ぐ) */
static int run(const char *path, struct vec *argv, const char *in, const char *out)
{
#ifdef NCC_HOST
    pid_t pid;
    int st;
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0)
        die("fork できない", path);
    if (pid == 0) {
        if (in) {
            int fd = open(in, O_RDONLY);
            if (fd < 0 || dup2(fd, 0) < 0)
                _exit(127);
            close(fd);
        }
        if (out) {
            int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0 || dup2(fd, 1) < 0)
                _exit(127);
            close(fd);
        }
        execv(path, argv->v);
        _exit(127);
    }
    if (waitpid(pid, &st, 0) < 0)
        die("waitpid が失敗した", path);
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#else
    int r;
    fflush(stdout);
    r = spawn((char *)path, argv->v, (char *)in, (char *)out);
    return r < 0 ? 127 : r;
#endif
}

/* ---- 置き場 ---- */

static const char *env_or(const char *name, const char *dflt)
{
    const char *v = getenv((char *)name);
    return v && v[0] ? v : dflt;
}

static char *bin_dir;

static char *tool(const char *name)
{
    char *d = cat2(bin_dir, "/");
    char *p = cat2(d, name);
    free(d);
    return p;
}

/* ---- 一時ファイル ---- */

static char *tmps[4];
static int ntmp;

static char *tmpname(const char *out, const char *suffix)
{
    char *p;
    if (out && strncmp(out, "/dev/", 5) != 0)
        p = cat2(out, suffix);
    else {
        char b[64];
        sprintf(b, "ncc-tmp-%d", (int)getpid());
        p = cat2(b, suffix);
    }
    tmps[ntmp++] = p;
    return p;
}

static void cleanup(void)
{
    int i;
    for (i = 0; i < ntmp; i++)
        remove(tmps[i]);
}

static int finish(int rc)
{
    cleanup();
    return rc;
}

/* ---- 命令行 ---- */

#define M_LINK 0
#define M_E 1
#define M_S 2
#define M_C 3

static int mode = M_LINK;
static char *out;
static char *lang;                      /* -x の値 (0 は拡張子で決める) */
static int medany;
static int want_deps;                   /* -MD / -MMD (-Wp, の形を含む) */
static char *dep_opt;                   /* 単独で書かれた -MD / -MMD */
static char *dep_file;                  /* -MF の値 */
static struct vec cppargs, ldargs, inputs;

static int has_prefix(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* -Wl,a,b,c を a b c に分けて nld1 へ */
static void split_wl(const char *a)
{
    const char *p = a + 4;
    while (*p) {
        const char *q = strchr(p, ',');
        long n = q ? (long)(q - p) : (long)strlen(p);
        char *w = xmalloc(n + 1);
        memcpy(w, p, (size_t)n);
        w[n] = 0;
        vadd(&ldargs, w);
        free(w);
        if (!q)
            break;
        p = q + 1;
    }
}

/* 問合せに答える。答えたら 1 */
static int query(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0) {
            printf("riscv32-stone-gcc (GCC) 4.6.4\n");
            return 1;
        }
        if (strcmp(a, "-dumpversion") == 0) {
            printf("4.6.4\n");
            return 1;
        }
        if (strcmp(a, "-print-file-name=include") == 0) {
            printf("%s\n", env_or("NCC_INCLUDE", NCC_LIB "/include"));
            return 1;
        }
        if (has_prefix(a, "-print-file-name=")) {
            printf("%s\n", a + 17);
            return 1;
        }
        if (strcmp(a, "-v") == 0) {
            fprintf(stderr, "gcc version 4.6.4 (stone)\n");
            return 1;
        }
    }
    return 0;
}

static void parse(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : 0;
        if (strcmp(a, "-E") == 0)
            mode = M_E;
        else if (strcmp(a, "-S") == 0)
            mode = M_S;
        else if (strcmp(a, "-c") == 0)
            mode = M_C;
        else if (strcmp(a, "-o") == 0) {
            if (!next)
                die("-o の後に名前が要る", 0);
            out = xstrdup(next);
            i++;
        } else if (has_prefix(a, "-o"))
            out = xstrdup(a + 2);
        else if (strcmp(a, "-x") == 0) {
            if (!next)
                die("-x の後に言語が要る", 0);
            lang = xstrdup(next);
            i++;
        } else if (strcmp(a, "-I") == 0 || strcmp(a, "-isystem") == 0 || strcmp(a, "-include") == 0
                   || strcmp(a, "-imacros") == 0 || strcmp(a, "-D") == 0 || strcmp(a, "-U") == 0
                   || strcmp(a, "-idirafter") == 0 || strcmp(a, "-iquote") == 0
                   || strcmp(a, "-MT") == 0 || strcmp(a, "-MQ") == 0) {
            if (!next)
                die("選択肢の後に値が要る", a);
            vadd(&cppargs, a);
            vadd(&cppargs, next);
            i++;
        } else if (has_prefix(a, "-I") || has_prefix(a, "-D") || has_prefix(a, "-U")
                   || strcmp(a, "-nostdinc") == 0 || strcmp(a, "-P") == 0 || strcmp(a, "-undef") == 0)
            vadd(&cppargs, a);
        else if (has_prefix(a, "-Wp,-MD,") || has_prefix(a, "-Wp,-MMD,")) {
            /* npp1 は cc1 の形を読む: -Wp,-MMD,<f> は -MMD <f> */
            const char *c = strchr(a + 4, ',');
            char *opt = xmalloc((long)(c - (a + 4)) + 1);
            memcpy(opt, a + 4, (size_t)(c - (a + 4)));
            opt[c - (a + 4)] = 0;
            vadd(&cppargs, opt);
            vadd(&cppargs, c + 1);
            free(opt);
            want_deps = 1;
        } else if (has_prefix(a, "-Wp,"))
            vadd(&cppargs, a + 4);
        else if (strcmp(a, "-MD") == 0 || strcmp(a, "-MMD") == 0) {
            /* npp1 は -MD / -MMD の次に記録の名前を取る。-MF と合わせて
             * preproc で渡す */
            dep_opt = xstrdup(a);
            want_deps = 1;
        } else if (strcmp(a, "-MF") == 0) {
            if (!next)
                die("-MF の後に名前が要る", 0);
            dep_file = xstrdup(next);
            i++;
        } else if (has_prefix(a, "-mcmodel="))
            medany = strcmp(a + 9, "medany") == 0;
        else if (has_prefix(a, "-Wl,"))
            split_wl(a);
        else if (strcmp(a, "-shared") == 0 || strcmp(a, "-s") == 0 || strcmp(a, "-r") == 0)
            vadd(&ldargs, a);
        else if (strcmp(a, "-T") == 0) {
            if (!next)
                die("-T の後に台本が要る", 0);
            vadd(&ldargs, "-T");
            vadd(&ldargs, next);
            i++;
        } else if (strcmp(a, "--param") == 0)
            i++;
        else if (strcmp(a, "-") == 0)
            vadd(&inputs, a);
        else if (a[0] == '-')
            ;                           /* -W*・-f*・-m*・-O*・-std=・-g など */
        else
            vadd(&inputs, a);
    }
}

#define L_C 0
#define L_ASMCPP 1
#define L_ASM 2

static int langof(const char *src)
{
    long n;
    if (lang) {
        if (strcmp(lang, "c") == 0)
            return L_C;
        if (strcmp(lang, "assembler-with-cpp") == 0)
            return L_ASMCPP;
        if (strcmp(lang, "assembler") == 0)
            return L_ASM;
        die("知らない言語", lang);
    }
    n = (long)strlen(src);
    if (n >= 2 && strcmp(src + n - 2, ".S") == 0)
        return L_ASMCPP;
    return L_C;
}

/* -o の無い時の依存の目標。GCC と同じく入力の名前から場所と拡張子を除いて
 * .o を付ける (標準入力は -) */
static char *dep_target(const char *src)
{
    const char *b = strrchr(src, '/');
    char *t, *dot;
    if (strcmp(src, "-") == 0)
        return xstrdup("-");
    t = xmalloc((long)strlen(src) + 3);
    strcpy(t, b ? b + 1 : src);
    dot = strrchr(t, '.');
    if (dot)
        *dot = 0;
    strcat(t, ".o");
    return t;
}

/* -MF の無い -MD / -MMD の記録の名前。GCC と同じく -o の名前の拡張子を .d に
 * 替え，-o が無ければ入力の名前から作る */
static char *dep_default(const char *src)
{
    char *t = dep_target(out ? out : src);
    t[strlen(t) - 1] = 'd';
    if (out && strchr(out, '/')) {
        /* dep_target は場所を除くので，-o の場所を戻す */
        const char *b = strrchr(out, '/');
        long n = (long)(b - out) + 1;
        char *r = xmalloc(n + (long)strlen(t) + 1);
        memcpy(r, out, (size_t)n);
        strcpy(r + n, t);
        free(t);
        return r;
    }
    return t;
}

/* 前処理する。dst が 0 なら標準出力へ */
static int preproc(const char *src, const char *dst, int l)
{
    struct vec a;
    char *path = tool("npp1");
    int rc;
    memset(&a, 0, sizeof a);
    vadd(&a, path);
    vadd(&a, "-P");
    vadd(&a, "-include");
    vadd(&a, env_or("NCC_PREDEF", NCC_LIB "/predef.h"));
    vcat(&a, &cppargs);
    if (dep_opt) {
        vadd(&a, dep_opt);
        vadd(&a, dep_file ? dep_file : dep_default(src));
    } else if (dep_file) {
        vadd(&a, "-MF");
        vadd(&a, dep_file);
    }
    if (medany) {
        vadd(&a, "-U__riscv_cmodel_medlow");
        vadd(&a, "-D__riscv_cmodel_medany=1");
    }
    /* 依存の印の目標は出力の名前にする (fixdep が読む)。依存を求められた
     * 時だけ付ける (gcc は -MD / -MMD の無い -MT を誤りとする) */
    if (want_deps) {
        vadd(&a, "-MT");
        vadd(&a, out ? out : dep_target(src));
    }
    vadd(&a, "-x");
    vadd(&a, l == L_C ? "c" : "assembler-with-cpp");
    vadd(&a, src);
    if (dst) {
        vadd(&a, "-o");
        vadd(&a, dst);
    }
    rc = run(path, &a, 0, 0);
    free(path);
    return rc;
}

static int compile(const char *in, const char *dst)
{
    struct vec a;
    char *path = tool("ncc1");
    int rc;
    memset(&a, 0, sizeof a);
    vadd(&a, path);
    if (medany)
        vadd(&a, "-mcmodel=medany");
    vadd(&a, "-o");
    vadd(&a, dst);
    vadd(&a, in);
    rc = run(path, &a, 0, 0);
    free(path);
    return rc;
}

static int assemble(const char *in, const char *dst)
{
    struct vec a;
    char *path = tool("nas1");
    int rc;
    memset(&a, 0, sizeof a);
    vadd(&a, path);
    vadd(&a, "-o");
    vadd(&a, dst);
    vadd(&a, in);
    rc = run(path, &a, 0, 0);
    free(path);
    return rc;
}

static int link_(void)
{
    struct vec a;
    char *path;
    int rc, c;
    /* 標準入力を組む試し (scripts/Kconfig.include の -m32 / -m64 など) は
     * 読み捨てて通す (tools/kbuildlog.sh の記録と同じ答え) */
    if (inputs.n > 0 && strcmp(inputs.v[0], "-") == 0) {
        while ((c = getchar()) != EOF)
            ;
        return 0;
    }
    if (!out)
        die("結合に -o が無い", 0);
    path = tool("nld1");
    memset(&a, 0, sizeof a);
    vadd(&a, path);
    vcat(&a, &ldargs);
    vadd(&a, "-o");
    vadd(&a, out);
    vcat(&a, &inputs);
    rc = run(path, &a, 0, 0);
    free(path);
    return rc;
}

int main(int argc, char **argv)
{
    const char *src;
    int l, rc;

    if (query(argc, argv))
        return 0;
    bin_dir = xstrdup(env_or("NCC_BIN", NCC_LIB));
    parse(argc, argv);
    if (mode == M_LINK)
        return link_();
    if (inputs.n != 1)
        die("入力は 1 つ", inputs.n ? inputs.v[1] : 0);
    src = inputs.v[0];
    l = langof(src);
    if (mode == M_E) {
        if (l == L_ASM)
            die("-E は前処理する言語だけ", lang);
        return preproc(src, out, l);
    }
    if (!out)
        die("-S / -c に -o が無い", 0);
    if (l == L_ASM) {
        if (mode != M_C)
            die("-S は .c だけ", src);
        return assemble(src, out);
    }
    {
        char *ti = tmpname(out, ".ncc.i");
        rc = preproc(src, ti, l);
        if (rc != 0)
            return finish(rc);
        if (l == L_ASMCPP) {
            if (mode != M_C)
                return finish((fprintf(stderr, "ncc: -S は .c だけ: %s\n", src), 1));
            return finish(assemble(ti, out));
        }
        if (mode == M_S)
            return finish(compile(ti, out));
        {
            char *ts = tmpname(out, ".ncc.s");
            rc = compile(ti, ts);
            if (rc != 0)
                return finish(rc);
            return finish(assemble(ts, out));
        }
    }
}
