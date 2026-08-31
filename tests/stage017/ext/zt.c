/* zlib を **我々の器で組んで，我々の OS の上で走らせる** (5.1)。
 *
 * 訳せることと動くことは別である。単位が 22/22 通っても，
 * それは「構文を拒まなかった」でしかない。ここは出来た書庫を繋いで
 * 実際に圧縮・伸長させ，**元に戻ること**を見る。
 *
 * 使う道は compress2 / uncompress —— deflate.c / inflate.c /
 * inftrees.c / inffast.c / trees.c / zutil.c / adler32.c / crc32.c /
 * compress.c / uncompr.c が実際に走る。 */
#include <stdio.h>
#include <string.h>
#include "zlib.h"

#define N 40000

static unsigned char src[N];
static unsigned char cmp[N * 2];
static unsigned char dec[N];

int main(void) {
  unsigned long clen;
  unsigned long dlen;
  unsigned long c1;
  unsigned long a1;
  int i;
  int r;
  int lv;

  /* 圧縮の効く並びと効かない並びを混ぜる */
  for (i = 0; i < N; i++) {
    if (i < N / 2) src[i] = (unsigned char)('a' + (i % 7));
    else src[i] = (unsigned char)((i * 1103515245 + 12345) >> 16);
  }

  c1 = crc32(0L, Z_NULL, 0);
  c1 = crc32(c1, src, N);
  a1 = adler32(0L, Z_NULL, 0);
  a1 = adler32(a1, src, N);
  printf("crc32 %08lx\n", c1);
  printf("adler32 %08lx\n", a1);
  printf("version %s\n", zlibVersion());

  for (lv = 1; lv <= 9; lv = lv + 4) {
    clen = sizeof cmp;
    r = compress2(cmp, &clen, src, (unsigned long)N, lv);
    if (r != Z_OK) { printf("compress2 %d rc=%d\n", lv, r); return 1; }
    dlen = sizeof dec;
    memset(dec, 0, sizeof dec);
    r = uncompress(dec, &dlen, cmp, clen);
    if (r != Z_OK) { printf("uncompress %d rc=%d\n", lv, r); return 1; }
    if (dlen != (unsigned long)N) { printf("len %d %ld\n", lv, (long)dlen); return 1; }
    if (memcmp(src, dec, N) != 0) { printf("differ %d\n", lv); return 1; }
    /* 圧縮したものが元より小さいこと (前半の並びが効く) */
    if (clen >= (unsigned long)N) { printf("nogain %d %ld\n", lv, (long)clen); return 1; }
    printf("level %d ok %ld\n", lv, (long)clen);
  }
  printf("z ok\n");
  return 0;
}
