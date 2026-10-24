/* 整数定数の接尾辞 (U / L)。境界値の定義で常用される */
int putc(int c);
int main(void) {
  unsigned u;
  long l;
  u = 4294967295U;
  l = 1L;
  putc('0' + (int)l);
  if (u > 100) putc('y');
  putc('\n');
  return 0;
}
