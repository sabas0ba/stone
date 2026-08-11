/* 64 bit の引数はまだ未対応。拒むことを固定する
 * (docs/stage015-tcc.md 6.2 の項目 11) */
int putc(int c);
int f(long long x) { return 0; }
int main(void) {
  f(1LL);
  putc('x');
  return 0;
}
