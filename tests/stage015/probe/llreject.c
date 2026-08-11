/* 未実装の演算は拒む。黙って下位語だけで計算して値を壊すより，
 * 拒むほうが正しい (docs/stage015-tcc.md 6.1) */
int putc(int c);
long long a;
long long b;
int main(void) {
  a = 1LL;
  b = a + a;
  putc('x');
  return 0;
}
