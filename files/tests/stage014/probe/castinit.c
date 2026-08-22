/* 大域初期化子の中のキャスト ((char *)"..." など)。zlib の z_errmsg で常用 */
int putc(int c);
char *em[3] = { (char *)"ab", (char *)"cd", (char *)"" };
int x = (int)7;
int main(void) {
  putc(em[0][0]);
  putc(em[1][1]);
  putc('0' + x);
  putc('\n');
  return 0;
}
