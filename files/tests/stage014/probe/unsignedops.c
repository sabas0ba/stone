/* 符号なしの比較・剰余・シフトと，負数の算術右シフト */
int putc(int c);
int main(void) {
  unsigned u;
  int s;
  u = 4294967295;
  s = -8;
  if (u > 100) putc('a');
  if ((u >> 28) == 15) putc('b');
  if ((s >> 1) == -4) putc('c');
  if ((u % 10) == 5) putc('d');
  putc('\n');
  return 0;
}
