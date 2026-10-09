/* **`*` の後ろには型修飾子が来る** (C89 6.5.4.1) ——
 *
 *     pointer: * type-qualifier-list_opt
 *
 * なので `int (*const f)(int)` は妥当な宣言子である。
 *
 * **括弧の外では前から通っていた** —— `int *const p;` は `pstars()` が
 * `*` の後ろの `const` / `volatile` を読み飛ばしている。**括弧の中の
 * 宣言子は `pstars` を通らない**ので、`fnpdec1()` だけがこの処理を
 * 持っていなかった。`cc15z` で揃えた。
 *
 * 表に出た形は GCC 4.7.4 の `libcpp/charset.c` 455 行 ——
 *
 *     static unsigned char
 *     conversion_loop (int (*const one_conversion)(iconv_t, const uchar **,
 *                                                 size_t *, uchar **, size_t *),
 *                      iconv_t cd, ...)
 *
 * `libcpp/charset` 1 単位が止まっていた (docs/stage017-gcc.md 8.3 の 11)。
 *
 * 本処理系は修飾子を検査に使わないので、読み飛ばすだけでよい。
 * **ただし読み飛ばした先が正しく繋がることは見る** —— 仮引数の位置が
 * ずれていれば値が違う。 */
int putc(int c);

int dbl(int x) { return x + x; }
int inc(int x) { return x + 1; }

/* charset.c 455 行と同じ形。const 付きの関数ポインタを先頭に置き、
 * その後ろにスカラを並べる */
int loop(int (*const one)(int), int x, int n) {
  int i;
  i = 0;
  while (i < n) { x = one(x); i = i + 1; }
  return x;
}

/* プロトタイプ側も抽象宣言子 + const で書く */
int pick(int (*const)(int), int);
int pick(int (*const f)(int), int x) { return f(x); }

/* volatile も読み飛ばすこと */
int vpick(int (*volatile f)(int), int x) { return f(x); }

int f(void) {
  int (*const g)(int) = dbl;    /* 局所の宣言。初期化子つき */
  int *const q = (int *)0;      /* 括弧の外。前から通っていた道 */

  if (loop(dbl, 1, 5) != 32) return 'n';    /* 1 -> 2 -> 4 -> 8 -> 16 -> 32 */
  if (loop(inc, 0, 42) != 42) return 'n';
  if (pick(dbl, 21) != 42) return 'n';
  if (vpick(inc, 41) != 42) return 'n';
  if (g(21) != 42) return 'n';
  if (q != (int *)0) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
