/* libc 環境部の検査 (docs/stage012-os.md 8 章 第 3 部)
 *
 * OS の上で純粋部 (string / stdlib) と環境部 (open / errno / brk 版
 * morecore) を組み合わせて動かす。malloc がベアメタルの上限 (1 MiB) を
 * 超えて確保できることが，供給源の差し替えが効いている証拠である。
 */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int putc(int c);

int puts(char *s) {
  while (*s) { putc(*s); s++; }
  return 0;
}

int main(void) {
  char *p;
  char *q;
  int fd;
  int n;
  char buf[32];

  /* 純粋部が OS の上でも無改変で動く */
  strcpy(buf, "abc");
  strcat(buf, "de");
  if (strlen(buf) == 5 && strcmp(buf, "abcde") == 0) putc('S');

  /* brk 版 morecore: ベアメタルの固定領域 (1 MiB) を超えて確保できる */
  p = (char *)malloc(3000000);
  if (p != NULL) {
    p[0] = 'x';
    p[2999999] = 'y';
    if (p[0] == 'x' && p[2999999] == 'y') putc('M');
    q = (char *)malloc(1000);
    if (q != NULL && q != p) putc('m');
    free(q);
    free(p);
  }

  /* open / read / close と errno */
  fd = open("data.txt", O_RDONLY);
  if (fd >= 0) {
    n = read(fd, buf, 32);
    buf[n] = 0;
    if (strcmp(buf, "abc\n") == 0) putc('R');
    close(fd);
  }
  errno = 0;
  if (open("none.txt", O_RDONLY) == -1 && errno == ENOENT) putc('E');

  /* 書き込みも POSIX の名前で通る */
  fd = open("out.txt", O_WRONLY | O_CREAT);
  if (fd >= 0) {
    if (write(fd, "ok\n", 3) == 3) putc('W');
    close(fd);
  }

  puts("\n");
  return 0;
}
