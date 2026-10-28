/* 64 bit の配列と変数の静的初期化 (第 3 部 その 2)。
 * cc15e までは下位語しか書かず**黙って壊れた** (通るが誤り)。
 * 負のリテラル・-1 (32 bit からの符号拡張)・列挙定数も見る */
int putc(int c);
unsigned long long a[3] = { 0x1122334455667788ULL, 0x99aabbccddeeff00ULL, 3 };
long long b = -0x0123456789abcdefLL;
unsigned long long c = -1;
long long r;
int hx(int v) {
  int i; int d;
  for (i = 7; i >= 0; i = i - 1) {
    d = (v >> (i * 4)) & 15;
    if (d < 10) putc('0' + d); else putc('a' + d - 10);
  }
  return 0;
}
int show(void) { int *q; q = (int *)&r; hx(q[1]); hx(q[0]); return 0; }
int main(void) {
  r = a[0]; show(); putc(':');
  r = a[1]; show(); putc(':');
  r = a[2]; show(); putc(':');
  r = b; show(); putc(':');
  r = c; show(); putc('\n');
  return 0;
}
