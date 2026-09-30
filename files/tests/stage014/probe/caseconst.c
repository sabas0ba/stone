/* case ラベルの定数式 (列挙定数・式)。zlib の inflate_table で常用 */
int putc(int c);
typedef enum { CODES, LENS, DISTS } codetype;
int f(codetype t) {
  switch (t) {
  case CODES: return 1;
  case LENS: return 2;
  case 1 + 1: return 3;
  }
  return 0;
}
int main(void) {
  putc('0' + f(CODES));
  putc('0' + f(LENS));
  putc('0' + f(DISTS));
  putc('\n');
  return 0;
}
