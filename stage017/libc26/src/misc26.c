/* misc26.c --- 第 26 世代で足した関数 (環境に依らないもの)
 *
 * GCC 4.7.4 の cc1 をリンクして名指しされた不足 (docs/stage017-gcc.md 8.15)。
 * C89 の関数 (labs / atol / atof / strtok / frexp / asctime / abort) と，
 * cc1 の configure が host (glibc) を見て「有る」とした関数 (alloca /
 * strcasecmp / strncasecmp / stpcpy / strnlen / canonicalize_file_name) である。
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <alloca.h>

void abort(void) {
  raise(SIGABRT);
  /* 処理器が戻った (または無視された) ときも終わる。C89 7.10.4.1 は
   * abort が呼び手へ戻らないことを求める。134 は 128 + SIGABRT */
  exit(134);
}

long labs(long n) {
  return n < 0 ? -n : n;
}

long atol(char *s) {
  return strtol(s, (char **)0, 10);
}

double atof(char *s) {
  return strtod(s, (char **)0);
}

/* 区切りの並びで s を切る。次の呼出しは s = NULL で続きを読む */
static char *tok_next;

char *strtok(char *s, char *sep) {
  char *b;
  if (s == 0) s = tok_next;
  if (s == 0) return 0;
  s = s + strspn(s, sep);
  if (*s == 0) {
    tok_next = 0;
    return 0;
  }
  b = s;
  s = s + strcspn(s, sep);
  if (*s) {
    *s = 0;
    tok_next = s + 1;
  } else {
    tok_next = 0;
  }
  return b;
}

size_t strnlen(char *s, size_t n) {
  size_t i;
  i = 0;
  while (i < n && s[i]) i = i + 1;
  return i;
}

char *stpcpy(char *d, char *s) {
  while ((*d = *s) != 0) {
    d = d + 1;
    s = s + 1;
  }
  return d;
}

int strcasecmp(char *a, char *b) {
  int x;
  int y;
  for (;;) {
    x = tolower(*a & 255);
    y = tolower(*b & 255);
    if (x != y || x == 0) return x - y;
    a = a + 1;
    b = b + 1;
  }
}

int strncasecmp(char *a, char *b, size_t n) {
  int x;
  int y;
  while (n > 0) {
    x = tolower(*a & 255);
    y = tolower(*b & 255);
    if (x != y || x == 0) return x - y;
    a = a + 1;
    b = b + 1;
    n = n - 1;
  }
  return 0;
}

char *canonicalize_file_name(char *path) {
  return realpath(path, (char *)0);
}

/* x = m * 2^e。double は IEEE 754 の 2 語 (下位語・上位語の順) である。
 * 指数の欄を 1022 (2^-1) に書き換えると仮数はそのまま [0.5, 1) に入る。
 * 非正規化数は 2^54 を掛けて正規化してから同じ手で分け，54 を引く */
double frexp(double x, int *e) {
  unsigned *w;
  int ex;
  int adj;
  w = (unsigned *)&x;
  adj = 0;
  ex = (int)((w[1] >> 20) & 2047);
  if (ex == 2047 || (ex == 0 && (w[1] & 0x7fffffff) == 0 && w[0] == 0)) {
    *e = 0;                       /* 無限大・NaN・0 はそのまま */
    return x;
  }
  if (ex == 0) {
    x = ldexp(x, 54);
    adj = 54;
    ex = (int)((w[1] >> 20) & 2047);
  }
  *e = ex - 1022 - adj;
  w[1] = (w[1] & 0x800fffff) | (1022U << 20);
  return x;
}

static char *wdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static char asc_buf[32];

char *asctime(struct tm *t) {
  char *wd;
  char *mo;
  wd = (t->tm_wday >= 0 && t->tm_wday < 7) ? wdays[t->tm_wday] : "???";
  mo = (t->tm_mon >= 0 && t->tm_mon < 12) ? months[t->tm_mon] : "???";
  sprintf(asc_buf, "%s %s %2d %02d:%02d:%02d %d\n", wd, mo, t->tm_mday,
          t->tm_hour, t->tm_min, t->tm_sec, t->tm_year + 1900);
  return asc_buf;
}

/* ---- alloca ----
 *
 * 呼び手の関数が戻るまで使える領域を返す。本物はスタックを伸ばすが，
 * 我々の cc は関数の枠の大きさを翻訳時に決めるので，呼出しで伸ばせない。
 * **ヒープから取り，取ったときのスタックの深さを控える。** 次に呼ばれた
 * とき，今より深い所 (番地が小さい所。スタックは下へ伸びる) で取った分は
 * その関数が既に戻っているので返す。同じ関数から何度呼んでも深さは同じ
 * なので返さない。
 *
 * **戻った後に触る使い方は本物と同じく誤りである。** 違うのは，返すのが
 * 次の alloca の呼出しまで遅れることだけである。alloca(0) は返すだけを行う */
struct alloca_hdr {
  struct alloca_hdr *next;      /* 1 つ前に取った領域 */
  char *depth;                  /* 取ったときのスタックの深さ */
};

static struct alloca_hdr *alloca_last;

void *alloca(size_t n) {
  char probe;
  char *depth;
  struct alloca_hdr *h;
  struct alloca_hdr *nx;
  depth = &probe;
  h = alloca_last;
  while (h != 0 && h->depth < depth) {
    nx = h->next;
    free(h);
    h = nx;
  }
  alloca_last = h;
  if (n == 0) return 0;
  h = (struct alloca_hdr *)malloc(sizeof(struct alloca_hdr) + n);
  if (h == 0) abort();
  h->next = alloca_last;
  h->depth = depth;
  alloca_last = h;
  return (void *)(h + 1);
}
