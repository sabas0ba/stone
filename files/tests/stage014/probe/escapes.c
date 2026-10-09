/* 文字エスケープ: 8 進・16 進・制御 */
int putc(int c);
int main(void) {
  putc('\101');
  putc('\x42');
  putc('\t');
  putc('\\');
  putc('\n');
  return 0;
}
