/* dir.c --- ディレクトリの読み出し (第 16 世代)
 *
 * 宣言は include/dirent.h。カーネルの getdents64 (61) を包む
 * (docs/stage016-os.md 7.2)。
 *
 * getdents64 は「器に入るだけ」返し，残りは次の呼出しに回す。したがって
 * readdir は次の 2 段になる。
 *
 *   1. 器に取り出していない件が残っていればそれを返す
 *   2. 尽きたら getdents64 をもう一度呼ぶ。0 が返ったら終わり
 *
 * DIR は 1 個で 1 KiB 強あるので malloc で取る。**固定の表にしないのは，
 * configure が入れ子で開くため**である (ディレクトリを辿りながら，
 * その中でまた開く)。
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

int getdents64(int fd, void *buf, int n);

/* getdents64 が返す 1 件の形 (Linux と同じ)。境界が保証されないので
 * 1 バイトずつ組む。64 bit の欄は下位 32 bit だけを見る (カーネルが
 * 32 bit なので上位は常に 0 である) */
#define D_INO    0
#define D_RECLEN 16
#define D_TYPE   18
#define D_NAME   19

static unsigned ld4u(char *p) {
  return (unsigned)(p[0] & 255) | ((unsigned)(p[1] & 255) << 8)
       | ((unsigned)(p[2] & 255) << 16) | ((unsigned)(p[3] & 255) << 24);
}

static int ld2u(char *p) {
  return (p[0] & 255) | ((p[1] & 255) << 8);
}

DIR *opendir(char *path) {
  DIR *d;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  d = (DIR *)malloc(sizeof(DIR));
  if (d == 0) {
    close(fd);
    return 0;
  }
  d->fd = fd;
  d->len = 0;
  d->pos = 0;
  return d;
}

struct dirent *readdir(DIR *d) {
  char *r;
  int rl;
  int n;

  if (d == 0) return 0;
  if (d->pos >= d->len) {
    n = getdents64(d->fd, d->buf, _DIRBUF);
    if (n <= 0) return 0;               /* 終わり，または誤り */
    d->len = n;
    d->pos = 0;
  }
  r = d->buf + d->pos;
  rl = ld2u(r + D_RECLEN);
  if (rl <= 0) return 0;                /* 壊れた並び。進めなくなる前に止める */
  d->pos = d->pos + rl;
  d->ent.d_ino = (unsigned long)ld4u(r + D_INO);
  d->ent.d_type = (unsigned char)(r[D_TYPE] & 255);
  strncpy(d->ent.d_name, r + D_NAME, 255);
  d->ent.d_name[255] = 0;
  return &d->ent;
}

int closedir(DIR *d) {
  int r;

  if (d == 0) return -1;
  r = close(d->fd);
  free(d);
  return r;
}
