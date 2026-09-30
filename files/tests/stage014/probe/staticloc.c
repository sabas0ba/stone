/* 関数内 static。呼出しをまたいで値が残る */
int putc(int c);
int next(void) {
  static int n;
  n = n + 1;
  return n;
}
int main(void) {
  next();
  next();
  putc('0' + next());
  putc('\n');
  return 0;
}
