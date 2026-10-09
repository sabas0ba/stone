/* 64 bit の実引数。前後に 32 bit の引数を置き，語数がずれても
 * それぞれ正しく渡ることを見る (docs/stage015-tcc.md 6.2 の項目 11) */
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
int take(int a, long long v, int b) {
  r = v;
  show();
  putc('0' + a);
  putc('0' + b);
  return 0;
}
int main(void) {
  long long x;
  x = 81985529216486895LL;
  take(1, x, 2);
  putc(':');
  take(3, x + 1LL, 4);
  putc('\n');
  return 0;
}
