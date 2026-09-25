/* **条件演算子の型は順序に依存しない** (C89 6.3.15) ——
 *
 *   一方の被演算子が空ポインタ定数であれば、結果は他方の型を持つ。
 *
 * `cc15x` までは結果の型を **then 側からだけ**取っていたので
 *
 *     (k ? 0 : p)->f
 *
 * の型が `int` になり、`->` が型の誤り (5) になった。`(k ? p : 0)->f` は
 * then 側がポインタなので**たまたま通っていた** —— 片方の並びだけが
 * 動くという出方である。
 *
 * **代入や返却では表に出ない。** そちらは左辺の型へ変換する道を通るので、
 * 条件式の型が誤っていても結果が合う。`->` を直に当てて初めて出る。
 *
 * 表に出た形は GCC 4.7.4 の `include/line-map.h` 518 行 ——
 *
 *     #define INCLUDED_FROM(SET, MAP)                                 \
 *       ((linemap_check_ordinary (MAP)->d.ordinary.included_from == -1) \
 *        ? NULL                                                       \
 *        : (&LINEMAPS_ORDINARY_MAPS (SET)[(MAP)->d.ordinary.included_from]))
 *
 * を `line-map.c` 266 行がそのまま `->` で辿る。我々の `NULL` は
 * `stddef.h` 21 行の `0` なので、then 側は整数定数 0 である
 * (docs/stage017-gcc.md 8.3 の 12)。
 *
 * **値まで見る。** 型だけ通しても指す先が違えば意味がない。 */
int putc(int c);

struct m { int f; int g; };
struct m a[3];

/* line-map.h と同じ形。-1 なら空、そうでなければ表の要素を指す */
struct m *at(int i) { return (i == -1) ? 0 : &a[i]; }

int f(void) {
  int i;
  struct m *p;

  a[0].f = 10; a[0].g = 11;
  a[1].f = 20; a[1].g = 21;
  a[2].f = 30; a[2].g = 31;

  /* then が空ポインタ定数。cc15x までは 5 で拒んでいた */
  i = 1;
  if ((i == -1 ? 0 : &a[i])->f != 20) return 'n';
  if ((i == -1 ? 0 : &a[i])->g != 21) return 'n';

  /* else が空ポインタ定数。前から通っていた道を壊していないこと */
  if ((i != -1 ? &a[i] : 0)->f != 20) return 'n';

  /* 添字が変われば指す先も変わる。型だけでなく値が通っていること */
  i = 2;
  if ((i == -1 ? 0 : &a[i])->f != 30) return 'n';
  i = 0;
  if ((i == -1 ? 0 : &a[i])->f != 10) return 'n';

  /* 空を返す枝。値としての 0 がそのまま出ること */
  if (at(-1) != 0) return 'n';
  if (at(1) != &a[1]) return 'n';

  /* 代入の道は前から通っていた (ここでは表に出ない形) */
  p = (i == -1) ? 0 : &a[1];
  if (p->f != 20) return 'n';

  /* 条件式そのものは int のまま扱えること (壊していない) */
  if ((i == 0 ? 1 : 2) + 1 != 2) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
