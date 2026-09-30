/* 32 bit から 64 bit への代入 (符号の伸ばし)。符号ありは符号ビットを
 * 伸ばし，符号なしは 0 を置く */
int putc(int c);
long long sv;
unsigned long long uv;
int lo(long long *p) { int *q; q = (int *)p; return q[0]; }
int hi(long long *p) { int *q; q = (int *)p; return q[1]; }
int hx(int v) {
  int i; int d;
  for (i = 7; i >= 0; i = i - 1) {
    d = (v >> (i * 4)) & 15;
    if (d < 10) putc('0' + d); else putc('a' + d - 10);
  }
  return 0;
}
int main(void) {
  int n;
  unsigned u;
  n = 0 - 2;
  u = 4294967294U;
  sv = n;
  hx(hi(&sv)); hx(lo(&sv)); putc(':');
  uv = u;
  hx(hi((long long *)&uv)); hx(lo((long long *)&uv));
  putc('\n');
  return 0;
}
