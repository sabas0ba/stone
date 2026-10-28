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
 *   alloca   20 段の再帰で 1000 バイトずつ取り，200 回繰り返す。続けて
 *            同じ深さで alloca を呼ぶ関数を 10 万回呼ぶ。1 回 4000 バイト
 *            なので，呼んだ関数の戻りで返さなければ 400 MB を取ろうとして
 *            ヒープ (プロセスの領域は 256 MiB) が尽きる (cc15ao が
 *            __alloca_release を呼ぶ。cc15an では abort で 134 になる)
 *   unlink   開いたファイルを unlink した後も fstat が長さを返す (kernel28)
 *   excl     O_CREAT | O_EXCL は在るファイルを EEXIST (17) で拒み，無い
 *            ファイルは作る。F_GETFD は FD_CLOEXEC (1) を返し，F_SETFD は
 *            立てる依頼だけを受ける (spawn は子に fd 3 以上を渡さない)。
 *            fcntl を可変引数にしたので，ポインタを渡すロック (F_SETLK)
 *            が引き続き受かることも見る
 *   qsort    鍵が等しい要素は元の並びを保つ (安定。glibc の qsort と同じ並び
 *            にするため。libc26)
 *   fmt      %.2f の半端の判定。0.005 は 2 進では僅かに大きいので 0.01，
 *            0.125 は丁度なので偶数の 0.12 (glibc と同じ。libc26)
 *   freopen  stdout を開き直した先 (y.txt) に書かれる */
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

struct kv { int k; int v; };
static int bykey(void *a, void *b) {
  return ((struct kv *)a)->k - ((struct kv *)b)->k;
}

static int flat(int n) {
  char *p;
  p = alloca(4000);
  p[3999] = (char)n;
  return p[3999];
}

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
  struct flock fl;
  struct kv kv[9];
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
  printf("alloca %d", s);
  s = 0;
  for (i = 0; i < 100000; i++) s = s + flat(i & 1);
  printf(" %d\n", s);
  f = fopen("z.txt", "w");
  fputs("12345678", f);
  fclose(f);
  fd = open("z.txt", 0);
  unlink("z.txt");
  i = fstat(fd, &st);
  printf("unlink %d %ld %d\n", i, st.st_size, access("z.txt", F_OK));
  close(fd);
  i = open("x.txt", O_WRONLY | O_CREAT | O_EXCL, 0600);
  e = errno;
  fd = open("w.txt", O_WRONLY | O_CREAT | O_EXCL, 0600);
  printf("excl %d %d %d", i, e, fd >= 3);
  printf(" %d %d %d", fcntl(fd, F_GETFD), fcntl(fd, F_SETFD, FD_CLOEXEC),
         fcntl(fd, F_SETFD, 0));
  fl.l_type = F_WRLCK; fl.l_whence = 0; fl.l_start = 0; fl.l_len = 0;
  printf(" %d", fcntl(fd, F_SETLK, &fl));
  fl.l_type = F_WRLCK;
  printf(" %d %d\n", fcntl(fd, F_GETLK, &fl), fl.l_type == F_UNLCK);
  close(fd);
  printf("misc %ld %ld %d %d %d %d\n", labs(-5), atol("123"), getpagesize(),
         environ[0] == 0, (int)(stpcpy(buf, "xyz") - buf), (int)strnlen("abcdef", 3));
  for (i = 0; i < 9; i++) { kv[i].k = (i * 5) % 3; kv[i].v = i; }
  qsort(kv, 9, sizeof(struct kv), bykey);
  printf("qsort");
  for (i = 0; i < 9; i++) printf(" %d", kv[i].v);
  printf("\n");
  printf("fmt %.2f %.2f %.1f %.0f\n", 0.005, 0.125, 2.45, 2.5);
  freopen("y.txt", "w", stdout);
  printf("freopen y\n");
  return 0;
}
