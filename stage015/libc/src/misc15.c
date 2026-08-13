/* misc15.c --- 第 4 部で足した雑多な関数 (実測: docs/stage015-tcc.md 11 章)
 *
 * ここにあるのは tcc が参照するが libc14 に無かったもののうち，
 * 独立した実装で足りるものである。printf 系の拡張は stdio.c，
 * strto 系は stdlib.c の側に足した。
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <time.h>

/* ---- setjmp / longjmp (setjmp.h の注記を見よ) ---- */

void longjmp(jmp_buf env, int val)
{
    (void)env;
    (void)val;
    fputs("longjmp: not supported (compile error path)\n", stderr);
    exit(1);
}

/* ---- 時刻 (時計が無いので固定値。固定点には好都合) ---- */

time_t time(time_t *t)
{
    if (t != NULL)
        *t = 0;
    return 0;
}

struct tm *localtime(time_t *t)
{
    static struct tm tm0;
    (void)t;
    tm0.tm_mday = 1;            /* 1970-01-01 00:00:00 */
    tm0.tm_year = 70;
    return &tm0;
}

/* ---- 環境 (単一プロセス・ルート直下しか無い) ---- */

char *getenv(char *name)
{
    (void)name;
    return NULL;                /* tcc は無ければ既定の経路を使う */
}

char *getcwd(char *buf, size_t size)
{
    if (size < 2)
        return NULL;
    buf[0] = '/';
    buf[1] = 0;
    return buf;
}

/* sfs に削除は無い。fopen("w") が上書きするので，出力前の unlink は
 * 何もしなくても目的 (前の内容を消す) は果たされる */
int unlink(char *path)
{
    (void)path;
    return 0;
}

int remove(char *path)
{
    return unlink(path);
}

char *strdup(char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

char *strerror(int e)
{
    static char buf[16];
    /* 番号を 10 進で返すだけ。メッセージ表はまだ持たない */
    sprintf(buf, "error %d", e);
    return buf;
}

/* ---- ldexp / fabs (math.h) ----
 *
 * d * 2^n は「2^n の bit の並びを組んで 1 回掛ける」。|n| <= 1023 の
 * 2^n は double で正確に表せるので，丸めは乗算の 1 回だけである。
 * 範囲の外は 2 回に分けて掛ける (途中で非正規化数を経由する)。 */

static double p2(int n)
{
    union { double d; unsigned long long u; } x;
    x.u = ((unsigned long long)(unsigned)(n + 1023)) << 52;
    return x.d;
}

double ldexp(double d, int n)
{
    while (n > 1023) {
        d = d * p2(1023);
        n = n - 1023;
    }
    while (n < -1022) {
        d = d * p2(-1022);
        n = n + 1022;
    }
    return d * p2(n);
}

double fabs(double d)
{
    union { double d; unsigned long long u; } x;
    x.d = d;
    x.u = x.u & 0x7fffffffffffffffULL;
    return x.d;
}

/* ---- sscanf (tcc が使う "%d.%d.%d" の形だけ) ---- */

int sscanf(char *s, char *fmt, ...)
{
    va_list ap;
    int n;
    int i;
    int v;
    int neg;
    int *out;

    va_start(ap, fmt);
    n = 0;
    i = 0;
    while (*fmt) {
        if (fmt[0] == '%' && fmt[1] == 'd') {
            fmt = fmt + 2;
            neg = 0;
            if (s[i] == '-') { neg = 1; i = i + 1; }
            if (s[i] < '0' || s[i] > '9') break;
            v = 0;
            while (s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i] - '0');
                i = i + 1;
            }
            out = va_arg(ap, int *);
            if (neg) *out = 0 - v;
            else *out = v;
            n = n + 1;
        } else if (*fmt == ' ') {
            fmt = fmt + 1;
            while (s[i] == ' ' || s[i] == '\t') i = i + 1;
        } else {
            if (s[i] != *fmt) break;
            fmt = fmt + 1;
            i = i + 1;
        }
    }
    va_end(ap);
    return n;
}
