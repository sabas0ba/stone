/* 整数の左辺と浮動小数点の右辺の複合代入を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.15 / stage015/cc15ap.sc。
 *
 * a op= b は a = a op b で (C89 6.3.16.2)，演算は double で行い，結果を
 * 0 方向へ切り捨てて左辺の型へ戻す。MPFR の eint.c が `prec += -d;`
 * (prec は long，d は double) と書く。cc15ao までは 5 で拒んでいた。
 *
 * **double で演算したことは値で見る。** 整数へ先に切り捨てると
 * `i += 0.5` を 2 回足しても i は変わらず，`i *= 1.5` は i * 1 になる。
 * 式の値 (書いた後の左辺) と，右辺の評価が 1 回であることも見る。 */
int putc(int c);

static void pn(long long v) {
  char b[24];
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

double d = 2.75;
float f = 1.5f;
int calls;
static double once(void) { calls = calls + 1; return 0.5; }

int main(void) {
  int i;
  unsigned u;
  short s;
  unsigned char c;
  long long ll;
  unsigned long long ull;
  int *pi;
  int arr[3];
  int k;

  i = 10; i += -d; pn(i);          /* 10 - 2.75 = 7.25 -> 7 */
  i = 10; i -= d; pn(i);
  i = 10; i *= d; pn(i);           /* 27.5 -> 27 */
  i = 10; i /= d; pn(i);           /* 3.63.. -> 3 */
  i = -10; i += d; pn(i);          /* -7.25 -> -7 (0 方向) */
  i = 3; i *= f; pn(i);            /* float の右辺: 4.5 -> 4 */
  i = 1; i += 0.5; i += 0.5; pn(i); /* 1.5 -> 1，1.5 -> 1 */
  i = 7; pn(i *= 1.5);             /* 式の値は書いた後の左辺 */
  nl();

  u = 4000000000U; u -= 0.5; pn(u);   /* u2d を通る: 3999999999.5 -> 3999999999 */
  u = 3; u /= 2.0; pn(u);
  s = -300; s *= 2.5; pn(s);
  c = 200; c += 50.9; pn(c);          /* 250.9 -> 250 */
  nl();

  ll = 10000000000LL; ll += 0.5; ll *= 3.0; pn(ll);
  ll = -9; ll /= 2.0; pn(ll);         /* -4.5 -> -4 */
  ull = 12345678901ULL; ull -= 1.0; pn((long long)ull);
  nl();

  /* 左辺の添字は 1 回だけ評価する。右辺も 1 回 */
  arr[0] = 1; arr[1] = 2; arr[2] = 3;
  k = 0;
  pi = arr;
  pi[k++] += once();
  arr[1] *= once() + 4.0;
  pn(arr[0]); pn(arr[1]); pn(arr[2]); pn(k); pn(calls);
  nl();
  return 0;
}
