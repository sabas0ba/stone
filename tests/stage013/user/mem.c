/* spawn を挟んだ親の保存の検査 (docs/stage013-tools.md 4 章)
 *
 *   - ヒープ (malloc で 200000 バイト) とフレームスタック上の配列が
 *     子の実行を挟んで保存される
 *   - 開いた fd と読み位置が子の実行を挟んで復元される
 *   - 子の終了コードが spawn の返り値になる (入れ子: sh -> mem -> args)
 *   - 無いプログラムは errno = ENOENT
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
  char *buf;
  char *av[3];
  char local[64];
  char two[4];
  int fd;
  int i;
  int r;
  int bad;

  for (i = 0; i < 64; i++) local[i] = (char)(i * 3);
  buf = malloc(200000);
  if (buf == NULL) { puts("malloc failed"); return 1; }
  for (i = 0; i < 200000; i++) buf[i] = (char)(i * 7);

  fd = open("in.txt", O_RDONLY);
  if (fd < 0) { puts("open failed"); return 1; }
  read(fd, two, 2);

  av[0] = "args";
  av[1] = "deep";
  av[2] = NULL;
  r = spawn("args", av, NULL, NULL);
  printf("child %d\n", r);

  read(fd, two + 2, 2);                 /* 読み位置が復元されていれば "ll" */
  close(fd);
  printf("fd %c%c%c%c\n", two[0], two[1], two[2], two[3]);

  r = spawn("nosuch", NULL, NULL, NULL);
  if (r == -1 && errno == ENOENT) puts("enoent ok");

  bad = 0;
  for (i = 0; i < 200000; i++)
    if (buf[i] != (char)(i * 7)) bad = 1;
  for (i = 0; i < 64; i++)
    if (local[i] != (char)(i * 3)) bad = 1;
  if (bad) { puts("corrupt"); return 1; }
  puts("mem ok");
  return 0;
}
