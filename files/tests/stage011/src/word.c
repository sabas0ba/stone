/* 自己適用: libc を使って書いた小さなプログラム (docs/stage011-libc.md 5 章)
 *
 * 空白区切りの語を strspn / strcspn で切り出し，先頭の文字を toupper で
 * 大文字にして出力する。string.o と ctype.o の両方をリンクする。
 */
#include <stddef.h>
#include <string.h>
#include <ctype.h>

int putc(int c);

char buf[64];

int main() {
  char *p;
  size_t n;
  size_t i;

  strcpy(buf, "  stone bootstraps a c89 libc  ");
  p = buf;
  while (1) {
    p += strspn(p, " ");
    if (*p == 0) break;
    n = strcspn(p, " ");
    *p = (char)toupper(*p);
    i = 0;
    while (i < n) { putc(p[i]); i++; }
    putc('.');
    p += n;
  }
  putc('\n');
  return 0;
}
