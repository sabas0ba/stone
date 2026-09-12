/* eot.c --- 標準入力を写し，末尾へ EOT を置く (Stage 13 第 3 部)
 *
 * ホスト側の `{ cat src; printf '\004'; }` のゲスト版である。
 *
 *   eot < cc12.sc > cc12.in
 *   cc < cc12.in > cc.o
 *
 * pp と cc は EOT (0x04) でしか読取りを止めない (どちらも凍結された
 * 世代で手を入れられない)。束ねを作る場合は bundle が末尾へ EOT を
 * 置くが，`.sc` のように **束ねを通さず直接 cc へ渡すソース** には
 * この道具が要る (docs/stage013-tools.md 7.2)。
 *
 * 中身は見ないので，任意のバイト列をそのまま通す。
 */
#include <stdio.h>
#include <unistd.h>

/* 読み取りの控え。**大域に置く。** cc の 1 関数のフレーム総サイズは
 * 2040 バイトまでである (docs/dev-notes.md 4 章) */
char b[4096];

int main(void) {
  int n;

  for (;;) {
    n = read(0, b, 4096);
    if (n <= 0) break;
    if (write(1, b, n) != n) return 1;
  }
  if (n < 0) return 1;
  b[0] = 4;                             /* EOT */
  if (write(1, b, 1) != 1) return 1;
  return 0;
}
