/* 引数リストが空の宣言 (古い C では「不明」を意味する) と前方宣言 */
int putc(int c);
int later();
int main(void) {
  putc('0' + later(4));
  putc('\n');
  return 0;
}
int later(int n) { return n + 1; }
