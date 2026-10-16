/* 配列の大きさの定数式 ([2 + 3] など)。#define した式で常用される */
int putc(int c);
char g[2 + 3];
char big[(2 + (900000 / 50))];
struct s { char sel[4 * 2]; int v; };
int main(void) {
  struct s t;
  g[4] = 'a';
  big[18001] = 'b';
  t.sel[7] = 'c';
  t.v = 0;
  putc(g[4]);
  putc(big[18001]);
  putc(t.sel[7]);
  putc('0' + t.v);
  putc('\n');
  return 0;
}
