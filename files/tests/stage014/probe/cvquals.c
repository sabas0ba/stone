/* const / volatile の局所宣言 */
int putc(int c);
int main(void) {
  volatile int v;
  const int c = 2;
  v = 1;
  putc('0' + v + c);
  putc('\n');
  return 0;
}
