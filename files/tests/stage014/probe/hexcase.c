/* 16 進リテラルの大文字 (0xFF)。現実のソースは大文字書きが多い */
int putc(int c);
int main(void) {
  if (0xFF == 255) putc('a');
  if (0xAb == 171) putc('b');
  if (0x7fffFFFF > 0) putc('c');
  putc('\n');
  return 0;
}
