/* 入れ子の初期化子と構造体の配列。表を静的に持つのは C の常套手段 */
int putc(int c);
struct p { int x; int y; };
struct p tab[2] = { { 1, 2 }, { 3, 4 } };
int main(void) {
  putc('0' + tab[0].y);
  putc('0' + tab[1].x);
  putc('\n');
  return 0;
}
