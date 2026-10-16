/* 可変長引数を別の関数へ渡す (vfprintf 相当)。libc の実装で必ず要る */
#include <stdarg.h>
int putc(int c);
int vsum(int n, va_list ap) {
  int t;
  int i;
  t = 0;
  for (i = 0; i < n; i++) t = t + va_arg(ap, int);
  return t;
}
int sum(int n, ...) {
  va_list ap;
  int t;
  va_start(ap, n);
  t = vsum(n, ap);
  va_end(ap);
  return t;
}
int main(void) {
  putc('0' + sum(3, 1, 2, 3));
  putc('\n');
  return 0;
}
