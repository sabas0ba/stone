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

/* ---- 記述子の経路 (fstat のため) ----
 *
 * カーネルは記述子から stat を引く手段を持たない (statat は経路で引く)。
 * open が開いた経路を**絶対形で**控え，fstat はそれを stat する。
 * 相対のまま控えると，開いた後に chdir した場合に別のファイルを引く。
 * 記述子はカーネルが 16 個までしか配らない (kernel27) */
#define FDMAX 16
static char fdpath[FDMAX][PATH_MAX];

void __fdpath_set(int fd, char *path) {
  int n;
  if (fd < 0 || fd >= FDMAX) return;
  fdpath[fd][0] = 0;
  if (path == 0) return;
  if (path[0] == '/') {
    if (strlen(path) < PATH_MAX) strcpy(fdpath[fd], path);
    return;
  }
  if (getcwd(fdpath[fd], PATH_MAX) == 0) {
    fdpath[fd][0] = 0;
    return;
  }
  n = (int)strlen(fdpath[fd]);
  if (n + 1 + (int)strlen(path) >= PATH_MAX) {
    fdpath[fd][0] = 0;            /* 控えられない。fstat は EBADF になる */
    return;
  }
  if (n > 1) {
    fdpath[fd][n] = '/';
    n = n + 1;
  }
  strcpy(fdpath[fd] + n, path);
}

int fstat(int fd, struct stat *st) {
  if (fd < 0 || fd >= FDMAX || fdpath[fd][0] == 0) {
    errno = EBADF;
    return -1;
  }
  return stat(fdpath[fd], st);
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
