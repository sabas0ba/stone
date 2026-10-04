/* libc26 で足した関数と ld18 を，kernel27 の上で確かめる (tests/stage017 第 12 部)。
 * docs/stage017-gcc.md 8.15 / stage017/libc26.md / stage015/ld18.md。
 *
 * 遠距離呼出し (`#pragma stone far_call`。cc15am) で訳し，ld18 で libc26 を
 * ライブラリの部品として組む。どれもホストに同名の関数があるが，ここで
 * 見たいのは我々の OS の上の振舞い (fstat が open の控えた経路を引く，
 * alloca が戻った関数の分を返す，freopen が同じ枠を使う) なので，期待値は
 * test.sh に書く。
 *
 *   tok      strtok が空の欄を飛ばす
 *   cmp      strcasecmp / strncasecmp
 *   frexp    48 = 0.75 * 2^6，-0.15625 = -0.625 * 2^-2
 *   fstat    open した記述子の大きさと種別。fileno(stdout) は 1
 *   access   在るファイルは 0，無いファイルは -1
 *   asctime  C89 7.12.3.1 の形
 *   alloca   20 段の再帰で 1000 バイトずつ取り，200 回繰り返す。戻った
 *            関数の分を返さなければ 4 MB を超えて取り続ける
 *   freopen  stdout を開き直した先 (y.txt) に書かれる */
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

static int depth(int n) {
  char *p;
  p = alloca(1000);
  memset(p, n, 1000);
  if (n > 0) return depth(n - 1) + p[999];
  return p[0];
}

int main(void) {
  char buf[64];
  char *t;
  int e;
  double m;
  struct stat st;
  FILE *f;
  int fd;
  struct tm tm;
  int i;
  int s;
  strcpy(buf, "a,b,,c");
  printf("tok");
  for (t = strtok(buf, ","); t; t = strtok(0, ",")) printf(" [%s]", t);
  printf("\ncmp %d %d %d\n", strcasecmp("Hello", "hELLO") == 0,
         strncasecmp("abcX", "ABCy", 3) == 0, strcasecmp("a", "b") < 0);
  m = frexp(48.0, &e);
  printf("frexp %d %d", (int)(m * 1000), e);
  m = frexp(-0.15625, &e);
  printf(" %d %d\n", (int)(m * 100000), e);
  f = fopen("x.txt", "w");
  fputs("hello\n", f);
  fclose(f);
  fd = open("x.txt", 0);
  i = fstat(fd, &st);
  printf("fstat %d %ld %d %d\n", i, st.st_size, S_ISREG(st.st_mode), fileno(stdout));
  close(fd);
  printf("access %d %d\n", access("x.txt", R_OK), access("nope", F_OK));
  tm.tm_sec = 52; tm.tm_min = 3; tm.tm_hour = 1; tm.tm_mday = 16;
  tm.tm_mon = 8; tm.tm_year = 73; tm.tm_wday = 0;
  printf("asctime %s", asctime(&tm));
  s = 0;
  for (i = 0; i < 200; i++) s = s + depth(20);
  printf("alloca %d\n", s);
  printf("misc %ld %ld %d %d %d %d\n", labs(-5), atol("123"), getpagesize(),
         environ[0] == 0, (int)(stpcpy(buf, "xyz") - buf), (int)strnlen("abcdef", 3));
  freopen("y.txt", "w", stdout);
  printf("freopen y\n");
  return 0;
}
