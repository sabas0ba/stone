/* 64 bit の返却はまだ未対応。拒むことを固定する */
int putc(int c);
long long f(void) { long long x; x = 1LL; return x; }
int main(void) {
  f();
  putc('x');
  return 0;
}
