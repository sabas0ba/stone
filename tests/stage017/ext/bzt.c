/* bzip2 を **我々の器で組んで，我々の OS の上で走らせる** (5.1)。
 *
 * BZ2_bzBuffToBuffCompress / Decompress は blocksort.c /
 * huffman.c / compress.c / decompress.c / crctable.c / randtable.c /
 * bzlib.c を一通り通す。 */
#include <stdio.h>
#include <string.h>
#include "bzlib.h"

#define N 40000

static char src[N];
static char cmp[N * 2];
static char dec[N];

int main(void) {
  unsigned int clen;
  unsigned int dlen;
  int i;
  int r;

  for (i = 0; i < N; i++) {
    if (i < N / 2) src[i] = (char)('a' + (i % 11));
    else src[i] = (char)((i * 1103515245 + 12345) >> 16);
  }

  clen = sizeof cmp;
  r = BZ2_bzBuffToBuffCompress(cmp, &clen, src, N, 9, 0, 30);
  if (r != BZ_OK) { printf("compress rc=%d\n", r); return 1; }
  if (clen >= (unsigned int)N) { printf("nogain %ld\n", (long)clen); return 1; }

  dlen = sizeof dec;
  memset(dec, 0, sizeof dec);
  r = BZ2_bzBuffToBuffDecompress(dec, &dlen, cmp, clen, 0, 0);
  if (r != BZ_OK) { printf("decompress rc=%d\n", r); return 1; }
  if (dlen != (unsigned int)N) { printf("len %ld\n", (long)dlen); return 1; }
  if (memcmp(src, dec, N) != 0) { printf("differ\n"); return 1; }

  printf("bz ok %ld\n", (long)clen);
  return 0;
}
