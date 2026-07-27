#include "shared.h"
int show(int v) {
  char *s;
  s = "v=";
  while (*s) { putc(*s); s = s + 1; }
  if (v >= LIMIT) putc('!');
  putc('0' + v / 10);
  putc('0' + v % 10);
  putc(' ');
  return 0;
}
