/* float / double の型・リテラル・記憶域・変換 (第 3 部 cc15h)。
 * 値は bit の並びで出す。リテラルの 10 進変換 (1e300 / 1.0e-5) も見る。
 * 四則はまだ無い (fparith が gap で捕まえる)。 */
int putc(int c);
double gd = 1.5;
float gf = 2.5f;
double ga[2] = { 0.25, -3.0 };
long long r;
int hx(int v) {
  int i; int d;
  for (i = 7; i >= 0; i = i - 1) {
    d = (v >> (i * 4)) & 15;
    if (d < 10) putc('0' + d); else putc('a' + d - 10);
  }
  return 0;
}
int show(void) { int *q; q = (int *)&r; hx(q[1]); hx(q[0]); return 0; }
double retd(void) { return 3.75; }
float retf(void) { return 0.5f; }
int main(void) {
  double d;
  float f;
  d = gd; r = *(long long *)&d; show(); putc(':');
  d = ga[0]; r = *(long long *)&d; show(); putc(':');
  d = ga[1]; r = *(long long *)&d; show(); putc(':');
  d = retd(); r = *(long long *)&d; show(); putc(':');
  f = gf; hx(*(int *)&f); putc(':');
  f = retf(); hx(*(int *)&f); putc(':');
  d = -gd; r = *(long long *)&d; show(); putc(':');
  d = (double)7; r = *(long long *)&d; show(); putc(':');
  d = (double)-2; r = *(long long *)&d; show(); putc(':');
  r = (long long)ga[1]; show(); putc(':');
  f = (float)gd; hx(*(int *)&f); putc(':');
  d = (double)gf; r = *(long long *)&d; show(); putc(':');
  d = 1e300; r = *(long long *)&d; show(); putc(':');
  d = 1.0e-5; r = *(long long *)&d; show(); putc('\n');
  return 0;
}
