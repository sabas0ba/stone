/* 第 25 世代 (libc25 + kernel27) の検査用プログラム
 * (tests/stage017 第 9 部。docs/stage017-gcc.md 8.9)。
 *
 * ホストと突き合わせられないもの (tests/stage017/probe/timesx.c では
 * 見られないもの) を見る。
 *
 *   1. 子の時間が親の cutime / cstime へ入ること。ホストの突き合わせでは
 *      spawn が使えない (ホストに無い) ので，ここで見る
 *   2. 経路の上限が定義どおり 255 バイト + NUL であること。kernel26 までは
 *      写す側の容量が 1 バイト足りず，255 バイトの経路を E2BIG で拒んでいた
 *
 * 引数 "child" で起動されたときは子として振る舞う: 自分の CPU 時間が
 * 3 tick (30 ms) 増えるまで計算して 0 で終わる。
 *
 * 出力は実行ごとに変わらない形 (y / n と errno) だけにしてある。 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/times.h>

static void yn(char *tag, int v) { printf("%s %s\n", tag, v ? "y" : "n"); }

/* 自分の CPU 時間 (utime + stime) を tick で返す */
static long cpu(void) {
  struct tms t;
  times(&t);
  return (long)(t.tms_utime + t.tms_stime);
}

/* CPU 時間が n tick 増えるまで計算する。打ち切りは経過 1000 tick */
static unsigned burn(long n) {
  struct tms t;
  clock_t r0;
  long c0;
  unsigned x;
  int i;
  r0 = times(&t);
  c0 = (long)(t.tms_utime + t.tms_stime);
  x = 1;
  while (cpu() - c0 < n && times(&t) - r0 < 1000)
    for (i = 0; i < 20000; i++) x = x * 1103515245 + 12345;
  return x;
}

/* s[0..n) を c で埋めて NUL で終える */
static void fill(char *s, int c, int n) {
  memset(s, c, (size_t)n);
  s[n] = 0;
}

/* 経路 p に空のファイルを作り，stat の結果を「タグ rc errno」で出す */
static void pathcase(char *tag, char *p) {
  FILE *f;
  struct stat st;
  int rc;
  f = fopen(p, "w");
  if (f != 0) fclose(f);
  errno = 0;
  rc = stat(p, &st);
  printf("%s %d %d\n", tag, rc, rc < 0 ? errno : 0);
}

int main(int argc, char **argv) {
  struct tms a;
  struct tms b;
  char *cargv[3];
  int rc;
  char d1[128];
  char d2[256];
  char p[300];
  char leaf[64];

  if (argc > 1 && strcmp(argv[1], "child") == 0) {
    burn(3);
    return 0;
  }

  printf("clk %ld\n", sysconf(_SC_CLK_TCK));

  /* 1. 子の時間 */
  times(&a);
  yn("pre-cut", a.tms_cutime == 0 && a.tms_cstime == 0);
  cargv[0] = "tmx";
  cargv[1] = "child";
  cargv[2] = 0;
  rc = spawn("tmx", cargv, 0, 0);
  printf("spawn %d\n", rc);
  times(&b);
  yn("cut", (long)(b.tms_cutime + b.tms_cstime) >= 3);
  /* 子の時間は親の utime / stime には入らない。spawn の間に親自身が
   * 使った時間は，子が使った時間 (3 tick 以上) より小さいはずである */
  yn("own-small", (long)(b.tms_utime + b.tms_stime)
                  - (long)(a.tms_utime + a.tms_stime)
                  < (long)(b.tms_cutime + b.tms_cstime));

  /* 2. 経路の上限。100 + 1 + 100 + 1 + 葉 で 254 / 255 / 256 バイト */
  printf("path-max %d %ld\n", PATH_MAX, pathconf("/", _PC_PATH_MAX));
  /* 無い経路は stat と同じ errno で -1。glibc はここで上限を返すので，
   * ホストとは突き合わせられない (probe/timesx.c の註) */
  errno = 0;
  printf("path-noent %ld %d\n", pathconf("tx_no_such_file", _PC_PATH_MAX),
         errno);
  fill(d1, 'd', 100);
  strcpy(d2, d1);
  strcat(d2, "/");
  fill(p, 'e', 100);
  strcat(d2, p);
  yn("mkdir", mkdir(d1, 0) == 0 && mkdir(d2, 0) == 0);
  fill(leaf, 'f', 52);
  strcpy(p, d2); strcat(p, "/"); strcat(p, leaf);
  pathcase("p254", p);
  fill(leaf, 'f', 53);
  strcpy(p, d2); strcat(p, "/"); strcat(p, leaf);
  pathcase("p255", p);
  fill(leaf, 'f', 54);
  strcpy(p, d2); strcat(p, "/"); strcat(p, leaf);
  pathcase("p256", p);
  return 0;
}
