/* ビットフィールド。ヘッダやプロトコル定義で広く使われる */
int putc(int c);
struct f {
  unsigned a : 3;
  unsigned b : 5;
};
int main(void) {
  struct f v;
  v.a = 5;
  v.b = 9;
  putc('0' + v.a);
  putc('0' + v.b);
  putc('\n');
  return 0;
}
