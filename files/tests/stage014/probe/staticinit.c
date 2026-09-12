/* 関数内 static の初期化子。zlib の inflate_table の表で常用 */
int putc(int c);
int f(void) {
  static int n = 3;
  static const unsigned short t[3] = { 30, 40, 50 };
  n = n + 1;
  return t[1] + n;
}
int main(void) {
  f();
  putc('0' + (f() - 40));
  putc('\n');
  return 0;
}
