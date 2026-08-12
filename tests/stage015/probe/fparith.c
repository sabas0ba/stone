/* 浮動小数点の四則 (cc15i で入る予定)。それまで cc は拒む。
 * 黙って整数の加算で bit を壊すのが最悪なので，拒むこと自体を検査する */
int putc(int c);
double a = 1.5;
double b = 2.0;
int main(void) {
  double c;
  c = a + b;
  putc('x');
  return 0;
}
