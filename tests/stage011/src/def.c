/* stddef.h / limits.h の検査 (docs/stage011-libc.md 3.1)
 *
 * どちらもヘッダだけで完結し，オブジェクトのリンクを要さない。
 * offsetof が実際の配置と一致することと，limits.h の値が
 * docs/stage010-c89.md 12 章の幅・符号と一致することを見る。
 */
#include <stddef.h>
#include <limits.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

struct s { char a; int b; char c[8]; short d; };

int main() {
  int i;
  unsigned u;
  char c;

  ok(sizeof(size_t) == 4);
  ok(sizeof(ptrdiff_t) == 4);
  ok(NULL == 0);

  ok(offsetof(struct s, a) == 0);
  ok(offsetof(struct s, b) == 4);
  ok(offsetof(struct s, c) == 8);
  ok(offsetof(struct s, d) == 16);

  ok(CHAR_BIT == 8);
  ok(CHAR_MIN == 0);            /* 素の char は符号なし */
  ok(CHAR_MAX == 255);
  c = CHAR_MAX;
  ok(c == 255);                 /* 実際に 255 を保持できる */
  ok(SCHAR_MIN == -128);
  ok(SCHAR_MAX == 127);
  ok(UCHAR_MAX == 255);
  ok(SHRT_MIN == -32768);
  ok(SHRT_MAX == 32767);
  ok(USHRT_MAX == 65535);

  i = INT_MAX;
  ok(i == 2147483647);
  i = INT_MIN;
  ok(i + 1 == -2147483647);
  u = UINT_MAX;
  ok(u > 2147483647);           /* 符号なし比較で最大値になっている */
  ok(LONG_MAX == INT_MAX);
  ok(ULONG_MAX == UINT_MAX);

  putc('\n');
  return 0;
}
