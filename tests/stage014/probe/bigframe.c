/* 2040 バイトを超えるローカル配列。実在のソースが普通に使う */
int putc(int c);
int f(void) {
  int a[257];
  int b[256];
  int i;
  for (i = 0; i < 257; i++) a[i] = i * 3;
  for (i = 0; i < 256; i++) b[i] = i + 7;
  if (a[256] != 768) return 1;
  if (b[255] != 262) return 2;
  return 0;
}
int main(void) {
  int r;
  r = f();
  putc('0' + r);
  putc('\n');
  return 0;
}
