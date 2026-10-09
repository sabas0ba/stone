/* 構造体に入れた関数ポインタ経由の呼出し (仮想表の手書き相当) */
int putc(int c);
int one(void) { return 1; }
int two(void) { return 2; }
struct op { char *name; int (*fn)(void); };
struct op ops[2] = { { "one", one }, { "two", two } };
int main(void) {
  putc('0' + ops[0].fn() + ops[1].fn());
  putc('\n');
  return 0;
}
