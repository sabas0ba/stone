/* 可変部の 2 語の値 (cc15k)。long long と double (float は既定の
 * 実引数拡張で double へ格上げ) を可変部に積み，va_arg が語数ぶん
 * 進めて読む (stage015 の stdarg.h)。printf("%llu") の前提。 */
#include <stdarg.h>
int putc(int c);
int f(char *sep, ...) {
  va_list ap;
  long long v;
  double d;
  int i;
  va_start(ap, sep);
  v = va_arg(ap, long long);
  d = va_arg(ap, double);
  i = va_arg(ap, int);
  putc('0' + (int)(v >> 32));
  putc('0' + (int)v);
  putc('0' + (int)d);
  putc('0' + i);
  putc(*sep);
  putc('\n');
  return 0;
}
int main(void) {
  float g;
  g = 4.0f;
  f(":", 0x300000002LL, g, 7);
  return 0;
}
