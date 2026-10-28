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
 * **ヒープから取り，取った関数の枠の基底 (深さ) を控える。**
 *
 * cc15ao 以降の cc は `alloca(n)` を `__alloca2(n, fp)` に書き換え，
 * alloca を呼んだ関数が戻る直前に `__alloca_release(fp)` を呼ぶ
 * (stage015/cc15ao.sc)。fp は呼んだ関数の枠の基底である。返すのは
 * 基底が fp 以下 (その呼出しと，より深い呼出し) の領域で，寿命は本物と
 * 同じく「呼んだ関数が戻るまで」になる。
 *
 * 名前で呼ばれる `alloca` (関数へのポインタを通した呼出しや，書き換えない
 * cc で訳したもの) は，alloca 自身の枠の位置を深さとして控え，次の
 * 呼出しで今より深い所の分を返す。こちらは同じ深さで繰り返し呼ばれる
 * 関数の分を返さない —— 返すのはより浅い所から呼ばれたときである。
 *
 * 戻った後に触る使い方は本物と同じく誤りである */
struct alloca_hdr {
  struct alloca_hdr *next;      /* 1 つ前に取った領域 */
  char *depth;                  /* 取った関数の枠の基底 */
};

static struct alloca_hdr *alloca_last;

/* 基底が depth より深い (番地が小さい) 領域を返す。eq が 1 なら同じ深さも返す。
 * スタックは下へ伸びるので，深い所で取った領域ほど新しい */
static void alloca_free_below(char *depth, int eq) {
  struct alloca_hdr *h;
  struct alloca_hdr *nx;
  h = alloca_last;
  while (h != 0 && (h->depth < depth || (eq && h->depth == depth))) {
    nx = h->next;
    free(h);
    h = nx;
  }
  alloca_last = h;
}

static void *alloca_take(size_t n, char *depth) {
  struct alloca_hdr *h;
  if (n == 0) return 0;
  h = (struct alloca_hdr *)malloc(sizeof(struct alloca_hdr) + n);
  if (h == 0) abort();
  h->next = alloca_last;
  h->depth = depth;
  alloca_last = h;
  return (void *)(h + 1);
}

void *alloca(size_t n) {
  char probe;
  alloca_free_below(&probe, 0);
  return alloca_take(n, &probe);
}

/* cc が alloca(n) を書き換えた形。fp は呼んだ関数の枠の基底 */
void *__alloca2(size_t n, char *fp) {
  /* 呼んだ関数より深い呼出しは既に戻っている */
  alloca_free_below(fp, 0);
  return alloca_take(n, fp);
}

/* alloca を呼んだ関数が戻る直前に cc が呼ぶ */
int __alloca_release(char *fp) {
  alloca_free_below(fp, 1);
  return 0;
}
