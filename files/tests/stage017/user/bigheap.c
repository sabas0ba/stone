/* kernel29 のユーザ領域 (1 GiB) を確かめる (tests/stage017 第 13 部)。
 * docs/stage017-gcc.md 8.15 / stage017/kernel29.c。
 *
 * 48 MiB を malloc で 16 回取る (768 MiB)。各ブロックの先頭と末尾に
 * 書き，全部取り終えてから読み戻す。最後に，取った領域の上端が
 * kernel28 の brk の上限 (0x9600_0000) を超えたかを出す。
 *
 *   heap <取れた MiB> <読み戻しの不一致の数> <0x9600_0000 を超えたか>
 *
 * kernel29 では 768 0 1 になる。kernel28 ではユーザ領域が 256 MiB なので，
 * 途中で malloc が NULL を返し，0x9600_0000 も超えない。 */
#include <stdio.h>
#include <stdlib.h>

#define BLK (48 * 1024 * 1024)
#define NBLK 16

int main(void) {
  char *p[NBLK];
  int i;
  int n;
  int bad;
  unsigned hi;
  n = 0;
  bad = 0;
  hi = 0;
  for (i = 0; i < NBLK; i++) {
    p[i] = malloc(BLK);
    if (p[i] == 0) break;
    p[i][0] = (char)i;
    p[i][BLK - 1] = (char)(i + 100);
    if ((unsigned)(p[i] + BLK) > hi) hi = (unsigned)(p[i] + BLK);
    n = n + 1;
  }
  for (i = 0; i < n; i++)
    if (p[i][0] != (char)i || p[i][BLK - 1] != (char)(i + 100)) bad = bad + 1;
  printf("heap %d %d %d\n", n * 48, bad, hi > 0x96000000u);
  return 0;
}
