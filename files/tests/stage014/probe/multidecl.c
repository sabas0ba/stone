/* 1 つの宣言に複数の宣言子。現実の C ではほぼ全てのファイルに現れる */
int putc(int c);
int main(void) {
  int a, b;
  a = 3;
  b = 4;
  putc('0' + a + b);
  putc('\n');
  return 0;
}
