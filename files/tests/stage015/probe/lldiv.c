/* 64 bit の除算・剰余。実行時支援 (stage015/rt64.c) の呼出しへ落ちる。
 * 符号つき・符号なし，剰余の符号，0 除算の値を見る */
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
int main(void) {
  long long a;
  long long b;
  unsigned long long ua;
  unsigned long long ub;
  a = 1000000000000LL;
  b = 7LL;
  r = a / b; show(); putc(':');
  r = a % b; show(); putc(':');
  a = 0LL - 1000000000000LL;
  r = a / b; show(); putc(':');
  r = a % b; show(); putc(':');
  ua = 0xffffffffffffffffLL;
  ub = 3LL;
  r = ua / ub; show(); putc(':');
  r = a / 0LL; show();
  putc('\n');
  return 0;
}
