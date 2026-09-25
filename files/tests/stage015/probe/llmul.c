/* 64 bit の乗算。上位語へ繰り上がる積と，32 bit を超える積を見る */
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
long long mk(int a) { long long x; x = 81985529216486895LL; return x + a; }
int main(void) {
  long long a;
  long long b;
  a = 4294967296LL;
  b = 3LL;
  r = a * b; show(); putc(':');
  a = 123456789LL;
  b = 1000000007LL;
  r = a * b; show(); putc(':');
  r = mk(0) * 2LL; show();
  putc('\n');
  return 0;
}
