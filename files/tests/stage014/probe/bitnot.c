/* 単項 ~ (ビット反転)。マスクの反転 (x &= ~m) で常用される */
int putc(int c);
int main(void) {
  int x;
  if (~0 == -1) putc('a');
  if (~5 == -6) putc('b');
  x = 255;
  x = x & ~(1 << 3);
  if (x == 247) putc('c');
  putc('\n');
  return 0;
}
