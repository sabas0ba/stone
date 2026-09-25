/* 入れ子のループから goto で抜ける。C では脱出の常套手段 */
int putc(int c);
int main(void) {
  int i;
  int j;
  int n;
  n = 0;
  for (i = 0; i < 3; i++) {
    for (j = 0; j < 3; j++) {
      if (i * 3 + j == 4) goto done;
      n = n + 1;
    }
  }
done:
  putc('0' + n);
  putc('\n');
  return 0;
}
