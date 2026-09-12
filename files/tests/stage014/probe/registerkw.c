/* register 記憶域クラス。古い C では頻出する */
int putc(int c);
int main(void) {
  register int i;
  int n;
  n = 0;
  for (i = 0; i < 3; i++) n = n + 1;
  putc('0' + n);
  putc('\n');
  return 0;
}
