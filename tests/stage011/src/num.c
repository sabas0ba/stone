/* strtol / atoi / abs / div の検査 (docs/stage011-libc.md 8.5)
 *
 * 空白と符号，基数の自動判定，endptr，数字なし，LONG_MAX / LONG_MIN への
 * 飽和，基数 36，div の 0 方向切捨てを検査する。
 * リンクするのは stdlib.o だけである。
 */
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

int main() {
  char *s;
  char *e;
  long v;
  div_t d;

  /* atoi */
  ok(atoi("123") == 123);
  ok(atoi("  -45") == -45);
  ok(atoi("12abc") == 12);
  ok(atoi("abc") == 0);

  /* 空白と符号 */
  s = " \t\n\v\f\r42";
  ok(strtol(s, &e, 10) == 42 && *e == 0);
  ok(strtol("+7", NULL, 10) == 7);
  ok(strtol("-0x10", NULL, 0) == -16);

  /* 基数の自動判定 (base 0) と明示 */
  ok(strtol("017", NULL, 0) == 15);
  ok(strtol("17", NULL, 0) == 17);
  ok(strtol("0x1F", NULL, 0) == 31);
  ok(strtol("0", NULL, 0) == 0);
  ok(strtol("17", NULL, 8) == 15);
  ok(strtol("FF", NULL, 16) == 255);
  ok(strtol("z", NULL, 36) == 35);
  ok(strtol("10", NULL, 36) == 36);

  /* "0x" の後ろに数字が無ければ "0" で止まる */
  s = "0x";
  v = strtol(s, &e, 16);
  ok(v == 0 && e == s + 1);
  s = "0xg";
  v = strtol(s, &e, 0);
  ok(v == 0 && e == s + 1);

  /* endptr: 数字の直後を指す */
  s = "1234rest";
  v = strtol(s, &e, 10);
  ok(v == 1234 && e == s + 4);

  /* 数字なし: 0 を返し endptr は先頭 (空白の後ろではない) */
  s = "  xyz";
  v = strtol(s, &e, 10);
  ok(v == 0 && e == s);

  /* 飽和 */
  ok(strtol("2147483647", NULL, 10) == LONG_MAX);
  ok(strtol("2147483648", NULL, 10) == LONG_MAX);
  ok(strtol("99999999999", NULL, 10) == LONG_MAX);
  ok(strtol("-2147483648", NULL, 10) == LONG_MIN);
  ok(strtol("-2147483649", NULL, 10) == LONG_MIN);
  ok(strtol("-99999999999", NULL, 10) == LONG_MIN);
  s = "2147483648xy";
  v = strtol(s, &e, 10);
  ok(v == LONG_MAX && e == s + 10);     /* 飽和しても桁は読み切る */

  /* abs */
  ok(abs(5) == 5);
  ok(abs(-5) == 5);
  ok(abs(0) == 0);

  /* div: 0 方向への切捨てと quot * denom + rem == numer */
  d = div(7, 2);
  ok(d.quot == 3 && d.rem == 1);
  d = div(-7, 2);
  ok(d.quot == -3 && d.rem == -1);
  d = div(7, -2);
  ok(d.quot == -3 && d.rem == 1);
  d = div(-7, -2);
  ok(d.quot == 3 && d.rem == -1);
  d = div(-9, 4);
  ok(d.quot * 4 + d.rem == -9);

  putc('\n');
  return 0;
}
