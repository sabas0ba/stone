char ob[16];
int pn(int v) {
  int n;
  if (v < 0) { putc('-'); v = -v; }
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n++; v /= 10; }
  while (n > 0) { n--; putc(ob[n]); }
  putc(' ');
  return 0;
}
unsigned int gu;
short gs;
unsigned short gus;
signed char gsc;
char gc;
struct m { char a; short b; int c; short d; };
struct m gm;
int main() {
  unsigned int u; int i; short s; unsigned short us; signed char sc; char c;
  long l; unsigned long ul;
  // 符号なしの除算・剰余・右シフト・比較
  u = 0 - 1;                    // 0xffffffff
  i = 0 - 1;
  pn(u / 1000000000);           // 4 (符号なし)
  pn(i / 1000000000);           // 0 (符号つき)
  pn(u >> 28);                  // 15
  pn(i >> 28);                  // -1 (算術シフト)
  pn(u > 1);                    // 1
  pn(i > 1);                    // 0
  pn(u % 10);                   // 4294967295 % 10 = 5
  // 幅
  pn(sizeof(short)); pn(sizeof(unsigned short)); pn(sizeof(long)); pn(sizeof(unsigned char));
  // 2 2 4 1
  // 切詰めと拡張
  s = 0 - 2;  pn(s);            // -2 (符号拡張)
  us = 0 - 2; pn(us);           // 65534 (0 拡張)
  sc = 0 - 2; pn(sc);           // -2
  c = 0 - 2;  pn(c);            // 254 (char は符号なし)
  // キャスト
  i = 300;
  pn((char)i);                  // 44
  pn((signed char)i);           // 44
  i = 200;
  pn((signed char)i);           // -56
  pn((short)70000);             // 4464
  pn((unsigned short)(0 - 1));  // 65535
  // 大域でも同じ
  gs = 0 - 3; gus = 0 - 3; gsc = 0 - 3; gc = 0 - 3; gu = 0 - 1;
  pn(gs); pn(gus); pn(gsc); pn(gc); pn(gu >> 24);   // -3 65533 -3 253 255
  l = 7; ul = 7;
  pn(l + ul);                   // 14
  // 構造体の整列 (char, short, int, short)
  gm.a = 1; gm.b = 2; gm.c = 3; gm.d = 4;
  pn(gm.a + gm.b + gm.c + gm.d);   // 10
  pn(sizeof(struct m));            // a@0 b@2 c@4 d@8 -> 12
  putc(10);
  return 0;
}
