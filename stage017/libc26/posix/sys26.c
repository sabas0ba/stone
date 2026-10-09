/* sys26.c --- 第 26 世代で足した関数 (カーネルと stdio に依るもの)
 *
 * GCC 4.7.4 の cc1 をリンクして名指しされた不足 (docs/stage017-gcc.md 8.15)。
 */
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h>

/* 記述子で stat する。カーネル (kernel28) の fstat2 (503) が statat2 (502)
 * と同じ 5 語を書く。開いたまま unlink したファイルも答えられる (カーネルは
 * 名前だけを消して実体を残す)。0 / 1 / 2 (端末) は EBADF である */
#define SYS_FSTAT2 503

/* 前置部の汎用スタブ (posix/sys.c と同じ宣言) */
int sys_ecall(int n, int a, int b, int c);

/* ナノ秒の 64 bit を秒へ直す (posix/sys.c の同名の関数と同じ) */
static time_t fns2sec(unsigned lo, unsigned hi) {
  unsigned long long ns;
  ns = ((unsigned long long)hi << 32) | (unsigned long long)lo;
  return (time_t)(ns / 1000000000ULL);
}

int fstat(int fd, struct stat *st) {
  unsigned raw[5];
  int r;
  r = sys_ecall(SYS_FSTAT2, fd, (int)raw, 0);
  if (r < 0) {
    errno = -r;
    return -1;
  }
  st->st_size = (long)raw[0];
  st->st_mtlo = raw[1];
  st->st_mthi = raw[2];
  st->st_type = (int)raw[3];
  /* sfs の巻は 1 つしか無い。「同じ装置か」は常に「はい」である */
  st->st_dev = 1;
  st->st_ino = (long)raw[4];
  st->st_mode = (st->st_type == S_TYPE_DIR) ? S_IFDIR : S_IFREG;
  st->st_mtime = fns2sec(raw[1], raw[2]);
  return 0;
}

int lstat(char *path, struct stat *st) {
  return stat(path, st);
}

int access(char *path, int mode) {
  struct stat st;
  (void)mode;                     /* 許可は無いので mode は答を変えない */
  return stat(path, &st);
}

int getpagesize(void) {
  return 4096;
}

static char *env_empty[1] = { 0 };
char **environ = env_empty;

int fileno(FILE *f) {
  return f->fd;
}

FILE *freopen(char *path, char *mode, FILE *f) {
  FILE *g;
  g = fopen(path, mode);
  if (g == 0) return 0;
  if (f->fd >= 3) close(f->fd);
  *f = *g;
  g->fd = -1;                     /* 新しく取った枠は空けて，f の枠を使う */
  return f;
}
