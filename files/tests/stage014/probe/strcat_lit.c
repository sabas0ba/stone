/* 隣接する文字列リテラルの連結 (C89 3.1.4)。real world では書式や
 * 長いメッセージを行に分けて書くのに常用される */
int putc(int c);
int main(void) {
  char *s;
  int i;
  s = "ab" "cd";
  for (i = 0; s[i]; i++) putc(s[i]);
  putc('\n');
  return 0;
}
