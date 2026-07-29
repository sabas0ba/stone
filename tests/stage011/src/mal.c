/* stdlib.h (malloc / free / calloc / realloc) の検査
 * (docs/stage011-libc.md 7.5)
 *
 * アドレスの具体値には依存せず，仕様として観測できる性質を検査する。
 * リンクするのは stdlib.o だけである (リンクの単位の検査を兼ねる)。
 */
#include <stddef.h>
#include <stdlib.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

char *blk[300];

int main() {
  char *a;
  char *b;
  char *c;
  char *q;
  char *r;
  unsigned i;
  unsigned j;
  int cnt;
  int k;

  /* 縮退した引数 */
  ok(malloc(0) == NULL);
  ok(calloc(0, 8) == NULL);
  ok(calloc(8, 0) == NULL);
  ok(calloc(65536, 65537) == NULL);     /* 積が size_t に収まらない */
  free(NULL);
  ok(1);                                /* free(NULL) が何もしないこと */
  ok(malloc(1048576) == NULL);          /* 総量超過 (ヘッダ分で溢れる) */

  /* 整列と，生きているブロックの非破壊 */
  a = (char *)malloc(17);
  b = (char *)malloc(17);
  c = (char *)malloc(17);
  ok(a != NULL && b != NULL && c != NULL);
  ok((unsigned)a % 4 == 0 && (unsigned)b % 4 == 0 && (unsigned)c % 4 == 0);
  for (i = 0; i < 17; i++) { a[i] = 1; b[i] = 2; c[i] = 3; }
  j = 0;
  for (i = 0; i < 17; i++) j = j + a[i] + b[i] + c[i];
  ok(j == 102);                         /* 17 * (1 + 2 + 3) */

  /* first-fit: free 直後に同じ大きさを取ると同じアドレスが返る */
  free(b);
  q = (char *)malloc(17);
  ok(q == b);
  free(a);
  free(q);
  free(c);

  /* calloc の 0 埋め (一度使って解放した領域の再利用で確認する) */
  a = (char *)malloc(64);
  for (i = 0; i < 64; i++) a[i] = 0xa5;
  free(a);
  b = (char *)calloc(16, 4);
  ok(b == a);                           /* 同じ領域が再利用されている */
  j = 0;
  for (i = 0; i < 64; i++) j = j + b[i];
  ok(j == 0);
  free(b);

  /* realloc */
  a = (char *)malloc(8);
  for (i = 0; i < 8; i++) a[i] = 'a' + i;
  q = (char *)realloc(a, 200);
  ok(q != NULL);
  j = 1;
  for (i = 0; i < 8; i++) if (q[i] != 'a' + i) j = 0;
  ok(j);                                /* 内容が保持されている */
  r = (char *)realloc(q, 4);
  ok(r == q);                           /* 縮小は在所のまま */
  r = (char *)realloc(NULL, 8);
  ok(r != NULL);                        /* realloc(NULL, n) は malloc */
  ok(realloc(r, 0) == NULL);            /* realloc(p, 0) は free して NULL */
  free(q);

  /* 枯渇と回復: 尽きるまで確保し，全て解放すれば全体が 1 ブロックに戻る */
  cnt = 0;
  while (cnt < 300) {
    blk[cnt] = (char *)malloc(4096);
    if (blk[cnt] == NULL) break;
    cnt = cnt + 1;
  }
  ok(cnt >= 250 && cnt < 300);          /* 1 MiB / (4096 + ヘッダ) 個で尽きる */
  for (k = 0; k < cnt; k++) free(blk[k]);
  a = (char *)malloc(1000000);
  ok(a != NULL);                        /* 併合がヒープ全体で機能している */
  free(a);

  putc('\n');
  return 0;
}
