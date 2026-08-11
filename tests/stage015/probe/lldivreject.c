/* 64 bit の除算はソフトウェアで割る必要があり，まだ入れていない。
 * 拒むことを固定する (docs/stage015-tcc.md 6.2 の項目 9) */
int putc(int c);
long long a;
long long b;
int main(void) {
  a = 100LL;
  b = 7LL;
  a = a / b;
  putc('x');
  return 0;
}
