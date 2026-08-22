/* morecore.c --- malloc の供給源 (brk 版)
 *
 * 設計は docs/stage012-os.md 6.2。Stage 11 第 2 部の予告どおり，
 * 差し替えるのはこの 1 関数だけである (stage011-libc.md 7.1)。
 * ベアメタル用の固定領域版は lib/morecore.c にある。利用者はどちらか
 * 一方を stdlib.o と並べてリンクする。
 */
#include <stddef.h>
#include <unistd.h>

#define UNIT   8        /* 割付けの単位 (struct hdr の大きさ。lib/stdlib.c) */
#define NALLOC 512      /* 最小の要求量 (単位数) */

void *morecore(size_t nu, size_t *got) {
  size_t req;
  void *p;

  req = nu;
  if (req < NALLOC) req = NALLOC;
  p = sbrk((int)(req * UNIT));
  if (p == (void *)-1) {
    /* 要求量ちょうどまで縮めて，もう一度だけ試す */
    req = nu;
    p = sbrk((int)(req * UNIT));
    if (p == (void *)-1) return NULL;
  }
  *got = req;
  return p;
}
