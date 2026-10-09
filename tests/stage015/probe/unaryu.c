/* 単項演算子の結果の型を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.15 / stage015/cc15an.sc。
 *
 * 単項の - と ~ の結果の型はオペランドを格上げした型である (C89 6.3.3.3)。
 * cc15am までは一律に int にしていたので，`~0U >> 1` が算術シフトになり，
 * `-1U > 5` が偽になっていた。GMP は `~(mp_limb_t) 0` や `-(mp_limb_t) x` を
 * 至る所で使う。単項の + (libdecnumber の `result=+1;`) は拒んでいた。
 *
 * **型を取り違えると，後に続く比較・割り算・右シフトの種類が変わる。**
 * 単項の結果をそのまま比較・割り算・シフトに渡した値を並べる。 */
int putc(int c);

static void pu(unsigned v) {
  char b[16];
  int n;
  n = 0;
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v != 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void pn(int v) {
  if (v < 0) { putc('-'); v = -v; }
  pu((unsigned)v);
}
static void nl(void) { putc('\n'); }

int main(void) {
  unsigned u;
  unsigned one;
  unsigned char c;
  unsigned short s;
  int i;
  u = 0;
  one = 1;
  c = 200;
  s = 60000;
  i = 5;
  pu(~u >> 1);
  pu(-one >> 28);
  pu((-one) / 2);
  pn(~u > 5);
  pn(-one > 5);
  pn((~u % 7));
  nl();
  /* 狭い符号なしは int に格上げされる。-c は負，~c も負 */
  pn(-c);
  pn(~c);
  pn(-s < 0);
  pn(~s < 0);
  nl();
  pn(+i);
  pn(+c);
  pn(+(-i));
  pu(+u - 1);
  nl();
  return 0;
}
