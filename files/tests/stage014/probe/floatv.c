/* 浮動小数点。処理系は整数のみ (docs/roadmap.md 1 章) */
int putc(int c);
int main(void) {
  double d;
  d = 1.5;
  putc('0' + (int)d);
  putc('\n');
  return 0;
}
