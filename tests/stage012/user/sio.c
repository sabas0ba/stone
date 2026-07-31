/* stdio の検査 (docs/stage012-os.md 8 章 第 4 部)
 *
 * printf の書式・境界と，FILE 経由のファイル往復を見る。
 */
#include <stdio.h>
#include <string.h>

int main(void) {
  FILE *f;
  char buf[64];
  int n;

  printf("%d %d %d\n", 0, -7, 2147483647);
  printf("%d\n", -2147483648);           /* INT_MIN は符号反転できない */
  printf("%u %x %05d|%5d|\n", 4294967295, 48879, 42, 42);
  printf("%c%s%%\n", 'A', "bc");

  /* FILE 経由の往復 */
  f = fopen("sio.txt", "w");
  if (f == NULL) { puts("open-w-failed"); return 1; }
  fprintf(f, "line1 %d\n", 5);
  fputs("line2\n", f);
  fclose(f);

  f = fopen("sio.txt", "r");
  if (f == NULL) { puts("open-r-failed"); return 1; }
  while (fgets(buf, 64, f) != NULL) {
    n = strlen(buf);
    printf("[%d]%s", n, buf);
  }
  if (feof(f)) puts("eof");
  fclose(f);

  /* 押し戻し */
  f = fopen("sio.txt", "r");
  n = fgetc(f);
  ungetc(n, f);
  printf("ungetc=%c%c\n", fgetc(f), fgetc(f));
  fclose(f);

  if (fopen("none.txt", "r") == NULL) puts("noent");
  return 0;
}
