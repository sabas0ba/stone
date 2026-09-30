/* libc15 の検査 (第 4 部)。printf の %llu / %.*s / %X / %f，snprintf の
 * 切り詰め，strtoul / strtoull / strtod，sscanf，getcwd，時刻の固定，
 * fseek / ftell / lseek (kernel15 の SYS_LSEEK) を一通り通す */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <math.h>

int main(void) {
  char buf[40];
  char *e;
  double d;
  FILE *f;
  jmp_buf jb;
  int n;

  printf("[%llu]", 12345678901234567ULL);
  printf("[%llx]", 0xdeadbeefcafeULL);
  printf("[%.*s]", 3, "abcdef");
  printf("[%5d]", 42);
  printf("[%X]", 48879);
  printf("[%.2f]", 3.75);
  n = snprintf(buf, 8, "%s", "toolongvalue");
  printf("[%d:%s]", n, buf);
  printf("[%lu]", strtoul("0x1f", &e, 0));
  printf("[%llu]", strtoull("18446744073709551615", NULL, 10));
  d = strtod("2.5e2", NULL);
  printf("[%d]", (int)d);
  printf("[%d]", (int)ldexp(1.0, 10));
  n = 0;
  sscanf("12.34.56", "%d.%d.%d", &n, (int *)buf, (int *)(buf + 4));
  printf("[%d,%d,%d]", n, *(int *)buf, *(int *)(buf + 4));
  printf("[%d]", (int)time(NULL));
  getcwd(buf, sizeof buf);
  printf("[%s]", buf);
  if (setjmp(jb) == 0) printf("[sj0]");

  f = fopen("t.txt", "w");
  fputs("hello world", f);
  fclose(f);
  f = fopen("t.txt", "r");
  fseek(f, 6, SEEK_SET);
  fgets(buf, sizeof buf, f);
  printf("[%s]", buf);
  fseek(f, -5, SEEK_END);
  printf("[%ld]", ftell(f));
  fclose(f);
  printf("\n");
  return 0;
}
