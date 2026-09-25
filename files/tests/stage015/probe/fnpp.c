/* 関数へのポインタへのポインタ `(**)` を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.11 / stage015/cc15ag.sc。
 *
 * C89 6.5.4.1 の pointer は「* type-qualifier-list_opt pointer_opt」で，
 * 括弧の中の `*` は 1 つとは限らない。GMP の gmp.h が
 *
 *     void __gmp_get_memory_functions (void *(**) (size_t), ...);
 *
 * と書き，GCC の gcc/ の単位は double-int.h から gmp.h を読む。cc15af までは
 * 2 つめの `*` で構文エラー (1) になっていた。
 *
 * **通るだけでは足りない。** 段を 1 つ数え違えれば，間接参照の回数がずれて
 * 別の番地を呼ぶ。どの形も値を出して突き合わせる。 */
int putc(int c);

static void pn(int v) {
  char b[16];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void nl(void) { putc('\n'); }

static int add1(int x) { return x + 1; }
static int dbl(int x) { return x * 2; }

/* 1. 名前を省いた形 (仮引数の宣言)。gmp.h と同じ */
static void pick(int (**)(int), int);

/* 2. 名前つきの形。out を通して関数を差し替える */
static void pick(int (**out)(int), int which) {
  if (which) *out = dbl;
  else *out = add1;
}

int main(void) {
  int (*f)(int);
  int (**pf)(int);
  f = add1;
  pf = &f;
  pn((*pf)(3));                 /* add1(3) = 4 */
  pick(pf, 1);
  pn(f(10));                    /* dbl(10) = 20: pick が f を書き換えた */
  pn((**pf)(7));                /* dbl(7) = 14 */
  pick(&f, 0);
  pn((*pf)(-5));                /* add1(-5) = -4 */
  nl();
  return 0;
}
