/* **浮動小数点定数の整数部は省ける** (C89 3.1.3.1) ——
 *
 *     fractional-constant:
 *       digit-sequence_opt . digit-sequence
 *       digit-sequence .
 *
 * なので `.0001` は妥当なリテラルである。`cc15z` までは `next()` が
 * **数字で始まるときしか** `lexnum()` へ渡していなかったので、
 * `.` が演算子として読まれて 1 (構文誤り) になった。
 *
 * 表に出た形は GCC 4.7.4 の `libcpp/symtab.c` の `approx_sqrt` ——
 *
 *     while (d > .0001);
 *
 * `libcpp/symtab` 1 単位が止まっていた (docs/stage017-gcc.md 8.3 の 13)。
 *
 * **`.` は他にも使われる。** 構造体の員と可変長引数の `...` を壊して
 * いないことを一緒に見る —— 次が数字のときだけ数として読む、という
 * 見分けが効いていなければここで落ちる。
 *
 * 値は 16 進の並びで出す。**「通った」だけでは足りない** ——
 * 整数部を 0 桁読んだ後の仮数と指数の組み立てが合っているかは、
 * ビットを見なければ判らない。 */
int putc(int c);

void ph(unsigned int v) {
  int i; int d;
  i = 28;
  while (i >= 0) {
    d = (v >> i) & 15;
    if (d < 10) putc('0' + d); else putc('a' + d - 10);
    i = i - 4;
  }
}

void pd(double x) {
  double *p;
  unsigned int *w;
  p = &x;
  w = (unsigned int *)p;
  ph(w[1]); ph(w[0]);
}

int f(void) {
  double a; double b; double c; double e; double g;
  double h; double n;

  a = .0001;      /* symtab.c の approx_sqrt と同じ形 */
  b = 0.0001;     /* 整数部を書いた同じ値 */
  c = .5;
  e = .5e2;       /* 指数つき */
  g = .5E-2;
  h = 1.;         /* 小数部を省いた形 (前から通っていた) */
  n = .125;       /* 2 の冪。丸めの入らない値 */

  /* **整数部の有無で値が変わらないこと。** 同じ定数の 2 通りの書き方 */
  if (a != b) return 'n';
  if (e != 50.0) return 'n';
  if (g != 0.005) return 'n';
  if (h != 1.0) return 'n';
  if (n != 0.125) return 'n';

  pd(a); putc(':');
  pd(c); putc(':');
  pd(e); putc(':');
  pd(n); putc(':');
  return 'y';
}

struct s { int m; int k; };
struct s v;

int va(char *fmt, ...) { return 1; }

int main() {
  /* '.' の他の使い道を壊していないこと */
  v.m = 3;
  v.k = 4;
  if (v.m + v.k != 7) { putc('n'); putc('\n'); return 0; }
  if (va("x", 1) != 1) { putc('n'); putc('\n'); return 0; }
  putc(f());
  putc('\n');
  return 0;
}
