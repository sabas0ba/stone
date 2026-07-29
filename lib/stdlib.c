/* stdlib.c --- 記憶域の管理 (C89 7.10.3)
 *
 * 設計は docs/stage011-libc.md 7 章。要点:
 *   - ヒープは .bss 上の固定領域 (1 MiB)。供給は morecore だけが行い，
 *     Stage 12 で brk へ差し替える境界をこの 1 関数に集める
 *   - 割付けは K&R 型のフリーリスト first-fit。各ブロックは 8 バイトの
 *     ヘッダ (次のフリーブロック，ヘッダ込みの大きさ) を持ち，free で
 *     隣接ブロックと併合する。大きさの単位はヘッダの大きさ (8 バイト)
 *   - realloc の複写と calloc の 0 埋めは内部ループで行う。memcpy や
 *     memset を呼ぶと，stdlib.o を使うすべてのプログラムが string.o の
 *     リンクを強制されるためである
 */
#include <stddef.h>
#include <stdlib.h>

struct hdr {
  struct hdr *next;             /* 次のフリーブロック (循環リスト) */
  size_t size;                  /* ヘッダ込みの大きさ (単位数) */
};

#define HEAPUNITS 131072        /* ヒープ全量: 131072 単位 * 8 バイト = 1 MiB */
#define NALLOC 512              /* morecore の最小供給量 (単位数) */

static struct hdr heap[HEAPUNITS];      /* 固定領域 (.bss なので初期値 0) */
static size_t heapused;                 /* 配り済みの単位数 */

static struct hdr base;         /* フリーリストの起点 (大きさ 0 の番兵) */
static struct hdr *freep;       /* 探索の開始点。0 なら未初期化 */

/* 供給源。固定領域から nu 単位以上を切り出してフリーリストへ足す。
 * 残量が最小供給量に満たなければ要求量ちょうどまで縮めて試みる。
 * Stage 12 で brk 版に差し替えるのはこの関数だけである */
static struct hdr *morecore(size_t nu)
{
  struct hdr *up;
  size_t req;

  req = nu;
  if (req < NALLOC) req = NALLOC;
  if (heapused + req > HEAPUNITS) req = nu;
  if (heapused + req > HEAPUNITS) return NULL;
  up = heap + heapused;
  heapused = heapused + req;
  up->size = req;
  free((void *)(up + 1));
  return freep;
}

void *malloc(size_t n)
{
  struct hdr *p;
  struct hdr *prev;
  size_t nunits;

  if (n == 0) return NULL;
  nunits = (n + sizeof(struct hdr) - 1) / sizeof(struct hdr) + 1;
  prev = freep;
  if (prev == NULL) {           /* 最初の呼出し: 空リストを作る */
    base.next = &base;
    base.size = 0;
    freep = &base;
    prev = &base;
  }
  p = prev->next;
  for (;;) {
    if (p->size >= nunits) {
      if (p->size == nunits) {  /* ちょうど: リストから外す */
        prev->next = p->next;
      } else {                  /* 末尾を切り出す */
        p->size = p->size - nunits;
        p = p + p->size;
        p->size = nunits;
      }
      freep = prev;
      return (void *)(p + 1);
    }
    if (p == freep) {           /* 一周した: 補充する */
      p = morecore(nunits);
      if (p == NULL) return NULL;
    }
    prev = p;
    p = p->next;
  }
}

void free(void *ap)
{
  struct hdr *bp;
  struct hdr *p;

  if (ap == NULL) return;
  bp = (struct hdr *)ap - 1;
  /* アドレス順の挿入位置を探す。リストの端 (最大アドレスと最小アドレスの
   * 間) に入る場合はそこで止める */
  p = freep;
  while (!(bp > p && bp < p->next)) {
    if (p >= p->next && (bp > p || bp < p->next)) break;
    p = p->next;
  }
  /* 番兵 (base) は大きさ 0 の印であり，.bss 上でヒープと隣接していても
   * 併合の対象にしない */
  if (bp + bp->size == p->next && p->next != &base) {   /* 後ろと併合 */
    bp->size = bp->size + p->next->size;
    bp->next = p->next->next;
  } else {
    bp->next = p->next;
  }
  if (p + p->size == bp && p != &base) {                /* 前と併合 */
    p->size = p->size + bp->size;
    p->next = bp->next;
  } else {
    p->next = bp;
  }
  freep = p;
}

void *calloc(size_t nmemb, size_t size)
{
  size_t n;
  size_t i;
  char *p;

  if (nmemb == 0 || size == 0) return NULL;
  n = nmemb * size;
  if (n / nmemb != size) return NULL;   /* 積が size_t に収まらない */
  p = (char *)malloc(n);
  if (p == NULL) return NULL;
  for (i = 0; i < n; i++) p[i] = 0;
  return (void *)p;
}

void *realloc(void *ap, size_t n)
{
  struct hdr *h;
  size_t nunits;
  size_t old;
  size_t i;
  char *q;
  char *s;

  if (ap == NULL) return malloc(n);
  if (n == 0) {
    free(ap);
    return NULL;
  }
  h = (struct hdr *)ap - 1;
  nunits = (n + sizeof(struct hdr) - 1) / sizeof(struct hdr) + 1;
  if (h->size >= nunits) return ap;     /* 現ブロックに収まる: 在所のまま */
  q = (char *)malloc(n);
  if (q == NULL) return NULL;
  old = (h->size - 1) * sizeof(struct hdr);     /* 旧ブロックの利用者領域 */
  s = (char *)ap;
  for (i = 0; i < old; i++) q[i] = s[i];        /* old < n が保証されている */
  free(ap);
  return (void *)q;
}
