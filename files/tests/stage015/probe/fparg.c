/* 浮動小数点の仮引数と実引数 (cc15j)。
 * 記号表の仮引数ビット表 (gdbm / gflm) と突き合わせ，int -> double の
 * 格上げ・float -> double・double -> int の切捨てなど C の変換を行う。
 * cc15h / cc15i に潜在していた「double を返す呼出しの返却がデータ
 * スタックをずらす」穴は，この検査が最初に踏んで cc15j で直った。 */
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
int sd(double d) { r = *(long long *)&d; show(); putc(':'); return 0; }
float half(float f) { return f / 2.0f; }
double mix(int a, double b, float c, long long d) {
  return a + b + c + (double)d;
}
int trunci(int v) { return v; }
int main(void) {
  float f;
  sd(1.5);                     /* double リテラルを double 仮引数へ */
  sd(2);                       /* int -> double の格上げ */
  sd(2.5f);                    /* float -> double の格上げ */
  sd(3LL);                     /* long long -> double */
  f = half(5.0f); hx(*(int *)&f); putc(':');
  f = half(6);                 /* int -> float の仮引数 */
  hx(*(int *)&f); putc(':');
  sd(mix(1, 0.5, 0.25f, 2LL));
  putc('0' + trunci(7.9));     /* double -> int 仮引数 (切捨て) */
  putc('\n');
  return 0;
}
