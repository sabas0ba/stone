/* 実引数の幅を宣言に合わせる (第 3 部 その 2)。
 * int の定数や変数を 64 bit の仮引数へ渡すと格上げされ (符号に応じて
 * 上位語が付く)，逆は下位語に切り詰められる。cc15e までは拒んだ */
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
long long pass(long long v) { return v; }
long long add3(long long a, int b, long long c) { return a + b + c; }
int low(int v) { return v; }
int main(void) {
  int m;
  m = -5;
  r = pass(7); show(); putc(':');            /* int 定数 -> 格上げ */
  r = pass(m); show(); putc(':');            /* 負の int -> 符号拡張 */
  r = add3(0x100000000LL, m, 2); show(); putc(':');
  r = (long long)low(0x0123456789abcdefLL); show(); putc('\n');  /* 切詰め */
  return 0;
}
