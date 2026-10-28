/* 64 bit の演算。桁上げ・借り・32 を超えるシフト・符号つき右シフト・
 * 比較を，上位語と下位語の両方で確かめる */
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
int show(void) {
  int *q;
  q = (int *)&r;
  hx(q[1]); hx(q[0]);
  return 0;
}
int main(void) {
  long long a;
  long long b;
  a = 4294967295LL;
  b = 1LL;
  r = a + b; show(); putc(':');       /* 桁上げが上位語へ届く */
  r = a - b; show(); putc(':');
  r = 0LL - 1LL; show(); putc(':');   /* 借りが上位語へ届く */
  r = a & 0xf0f0f0f0LL; show(); putc(':');
  r = 1LL << 40; show(); putc(':');   /* 32 を超えるシフト */
  r = (0LL - 16LL) >> 2; show(); putc(':');  /* 符号つき右シフト */
  putc('0' + (a > b));
  putc('0' + (a < b));
  putc('0' + (a == a));
  putc('0' + (a != a));
  putc('\n');
  return 0;
}
