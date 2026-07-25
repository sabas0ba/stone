// 仕様 2.3/2.5 節: 関数・再帰・前方参照 (pr は fib より前に呼ばれる)。
// fib(15) = 610 を 10 進で出力する。
int pr(int n) {
  if (n > 9) pr(n / 10);
  putc(48 + n % 10);
  return 0;
}
int fib(int n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}
int main() {
  pr(fib(15));
  putc(10);
  return 0;
}
