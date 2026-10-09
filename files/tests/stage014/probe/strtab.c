/* 文字列ポインタの表。診断メッセージの表などで常用される */
int putc(int c);
char *msg[2] = { "hi", "yo" };
int main(void) {
  int i;
  int j;
  for (i = 0; i < 2; i++)
    for (j = 0; msg[i][j]; j++) putc(msg[i][j]);
  putc('\n');
  return 0;
}
