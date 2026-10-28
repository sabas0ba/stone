/* 駆動役が「まともな C」を通せるかを見る。hello.c だけだと
 * printf を 1 回呼ぶ経路しか通らない。
 *
 * ここで使うのは stdio / string / stdlib と，構造体・配列・ポインタ・
 * 再帰・可変長引数の受け側である。**libc の 1 揃いが /lib から正しく
 * 引かれているか**が主な狙いで，1 つでも欠けるとリンクで落ちる */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pt {
  int x;
  int y;
};

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int cmp(const void *a, const void *b) {
  return *(const int *)a - *(const int *)b;
}

int main(void) {
  struct pt p;
  int v[5];
  char buf[64];
  char *heap;
  int i;

  p.x = 3;
  p.y = 4;
  printf("struct %d\n", p.x * p.x + p.y * p.y);

  printf("fib %d\n", fib(12));

  v[0] = 5; v[1] = 1; v[2] = 4; v[3] = 2; v[4] = 3;
  qsort(v, 5, sizeof(int), cmp);
  printf("sort");
  for (i = 0; i < 5; i = i + 1) printf(" %d", v[i]);
  printf("\n");

  sprintf(buf, "%s-%d-%x", "fmt", 42, 255);
  printf("sprintf %s\n", buf);

  heap = malloc(32);
  strcpy(heap, "malloc ok");
  printf("%s\n", heap);
  free(heap);

  printf("strlen %d\n", (int)strlen("0123456789"));
  return 0;
}
