/* 第 14 世代の libc の検査 (docs/stage014-external.md 10 章)
 *
 *   printf: l 修飾 (%ld %lu %lx)・左詰め (%-)・%s の幅・0 詰めの併用
 *   sprintf / vsprintf: 緩衝への書式化と返り値 (書いた長さ)
 *   assert: 成立 (何も起きない)
 *
 * assert の失敗は別のプログラム (abrt.c) で見る (exit(1) するため)。
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
  char b[64];
  long lv;
  int n;

  lv = -12345;
  printf("[%ld]\n", lv);
  printf("[%lu]\n", 4294954951LU);
  printf("[%lx]\n", 255L);
  printf("[%-6d]\n", 42);
  printf("[%6d]\n", 42);
  printf("[%06d]\n", 42);
  printf("[%-8s][%8s]\n", "ab", "cd");
  n = sprintf(b, "%s=%03d", "x", 7);
  printf("[%s] n=%d\n", b, n);
  assert(n == 5);
  assert(strcmp(b, "x=007") == 0);
  puts("lib14 ok");
  return 0;
}
