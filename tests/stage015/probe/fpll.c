/* 関数へのポインタを通した呼出しの実引数の変換を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.15 / stage015/cc15aq.sc。
 *
 * 実引数は仮引数の型へ変換して渡す (C89 6.3.2.2)。我々の呼出し規約では
 * 呼ばれた側が語を数えて取り出すので，幅が違うと以降の実引数がすべて
 * ずれる。cc15ap までは関数へのポインタの型が仮引数を持たず，実引数を
 * そのまま積んでいた。GCC の cc1 は `targetm.calls.return_pops_args
 * (fndecl, funtype, stack_size)` の 64 bit の stack_size を int の仮引数へ
 * 渡し，funtype の位置がずれて落ちた。
 *
 * 見るもの: 64 bit -> int / int -> 64 bit / int -> double / double -> float /
 * double -> int の変換，構造体のメンバ・局所・配列の関数へのポインタ，
 * 関数型の typedef，関数型の仮引数，(*f)(x)，可変長の関数をポインタで
 * 呼ぶ形 (可変部の積み方)，仮引数に関数ポインタを持つ関数ポインタの
 * typedef (GCC の tree.h の walk_tree_lh。内側の宣言子が外側の名前を
 * 消していた)。 */
#include <stdarg.h>
int putc(int c);

static void pn(long long v) {
  char b[24];
  int n;
  n = 0;
  if (v < 0) { putc('-'); v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}

struct hooks {
  int (*pops) (char *a, char *b, int size);
  long long (*wide) (int tag, long long v, int tail);
  int (*fl) (float f, double d, int k);
};

static int pops_impl(char *a, char *b, int size) {
  return (a[0] - '0') * 1000 + (b[0] - '0') * 100 + size;
}
static long long wide_impl(int tag, long long v, int tail) {
  return v * 10 + tag * 3 + tail;
}
static int fl_impl(float f, double d, int k) {
  return (int)(f * 100.0f) + (int)(d * 10.0) + k;
}

struct hooks h = { pops_impl, wide_impl, fl_impl };

typedef int binop_t (int, long long);
static int sub2(int a, long long b) { return a - (int)b; }

static int apply(binop_t f, int a, int b) { return f(a, b); }

typedef int (*walk_fn) (int *, int (*) (int), long long);
static int dbl(int x) { return 2 * x; }
static int walk_impl(int *p, int (*g) (int), long long k) { return g(*p) + (int)k; }

static int vsum(int n, ...) {
  va_list ap;
  int s;
  int i;
  s = 0;
  va_start(ap, n);
  for (i = 0; i < n; i++) s = s * 10 + va_arg(ap, int);
  va_end(ap);
  return s;
}

int main(void) {
  long long big;
  int (*fp) (char *, char *, int);
  binop_t *bp;
  int (*vp) (int, ...);
  int (*tab[2]) (char *, char *, int);
  walk_fn w;
  int four;
  big = 7;
  fp = pops_impl;
  tab[1] = pops_impl;
  bp = sub2;
  vp = vsum;
  w = walk_impl;
  four = 4;
  pn(h.pops("1", "2", big));          /* 64 bit -> int */
  pn(fp("3", "4", big + 1));
  pn(tab[1]("5", "6", 9));
  pn(h.wide(2, 5, 1));                 /* int -> 64 bit */
  pn(h.wide(1, 4000000000LL, big));
  pn(h.fl(1.5, 2, 3.9));               /* double -> float，int -> double，double -> int */
  pn(bp(10, 3));
  pn(apply(sub2, 20, 6));
  pn((*sub2)(9, 4));
  pn(vp(3, 1, 2, 3));                  /* 可変部は逆順に積む */
  pn(vp(2, 4, 5));
  pn(w(&four, dbl, 1));                /* int -> 64 bit (typedef の関数ポインタ) */
  putc('\n');
  return 0;
}
