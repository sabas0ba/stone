/* morecore.c --- malloc の供給源 (固定領域版。ベアメタル用)
 *
 * ヒープを .bss 上の固定領域 1 MiB に置く (docs/stage011-libc.md 7.1)。
 * OS の上では brk 版 (lib/posix/morecore.c) を代わりに並べる
 * (docs/stage012-os.md 6.2)。
 */
#include <stddef.h>

#define UNIT      8             /* 割付けの単位 (struct hdr の大きさ) */
#define HEAPUNITS 131072        /* 131072 単位 * 8 バイト = 1 MiB */
#define NALLOC    512           /* 最小の要求量 (単位数) */

/* 配列の大きさは定数式ではなく数値で書く (cc は宣言子の中で定数畳み込みを
 * 行わない)。1048576 = HEAPUNITS * UNIT である */
static char heap[1048576];              /* 固定領域 (.bss なので初期値 0) */
static size_t used;                     /* 配り済みの単位数 */

void *morecore(size_t nu, size_t *got) {
  size_t req;

  req = nu;
  if (req < NALLOC) req = NALLOC;
  if (used + req > HEAPUNITS) req = nu;
  if (used + req > HEAPUNITS) return NULL;
  *got = req;
  used = used + req;
  return (void *)(heap + (used - req) * UNIT);
}
