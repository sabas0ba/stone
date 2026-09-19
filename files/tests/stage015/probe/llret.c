/* 64 bit の返却。返した値どうしの演算と，32 bit からの符号伸ばしも見る */
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
long long mk(int a) {
  long long x;
  x = 81985529216486895LL;
  return x + a;
}
long long widen(int a) { return a; }
int main(void) {
  r = mk(1);
  show(); putc(':');
  r = mk(0) + mk(0);
  show(); putc(':');
  r = widen(0 - 2);
  show();
  putc('\n');
  return 0;
}
