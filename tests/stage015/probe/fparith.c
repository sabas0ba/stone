/* 浮動小数点の四則・比較・条件 (cc15i)。
 * 四則は double へ持ち上げて実行時支援を呼び，float どうしは最後に
 * 1 回だけ丸め直す。比較は __dcmp の -1/0/1/2 を突き合わせる。
 * NaN (すべての比較が偽，!= だけ真，条件では真) と -0.0 (条件で偽) が
 * C の規則どおりであることも見る。
 * 仮引数の浮動小数点はまだ無いので大域で渡している (次の世代)。 */
int putc(int c);
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
double t;                    /* 仮引数の浮動小数点はまだ無いので大域で渡す */
int sd(void) { r = *(long long *)&t; show(); putc(':'); return 0; }
double a = 1.5;
double b = 0.25;
float fa = 2.5f;
float fb = 0.5f;
double z = 0.0;
double nz = -0.0;
int main(void) {
  float f;
  double q;
  t = a + b; sd();
  t = a - b; sd();
  t = a * b; sd();
  t = a / b; sd();
  t = a + 1; sd();          /* int と混ぜる */
  t = 2 * b; sd();
  f = fa + fb;  hx(*(int *)&f); putc(':');   /* float どうし -> float */
  f = fa / fb;  hx(*(int *)&f); putc(':');
  putc('0' + (a > b)); putc('0' + (a < b)); putc('0' + (a == a));
  putc('0' + (a != b)); putc('0' + (a <= b)); putc('0' + (b <= b));
  putc(':');
  q = z / z;                                  /* NaN */
  putc('0' + (q == q)); putc('0' + (q != q)); putc('0' + (q < a));
  putc(':');
  if (a) putc('t'); else putc('f');
  if (z) putc('t'); else putc('f');
  if (nz) putc('t'); else putc('f');          /* -0.0 は偽 */
  if (q) putc('t'); else putc('f');           /* NaN は真 */
  putc(':');
  putc('0' + (int)(a + b));                   /* 1.75 -> 1 */
  putc('\n');
  return 0;
}
