/* 数値変換 (strtol / strtoul / strtoll / atoi / abs / div) の縁を，
 * ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.3)。
 *
 * 見るのは値だけではなく **endptr がどこで止まったか** である。
 * 「変換できた部分の次」を指すのが C89 7.10.1.5 の定めで，1 文字でも
 * ずれると呼び手の解析が静かに狂う。
 *
 * 溢れ (LONG_MAX への飽和) は測らない。我々の long は 32 bit，
 * ホストは 64 bit なので，飽和する値そのものが違う。 */
#include <stdio.h>
#include <stdlib.h>

static void l(char *tag, char *s, int base) {
  char *e;
  long v;
  e = 0;
  v = strtol(s, &e, base);
  printf("%s %ld @%d\n", tag, v, (int)(e - s));
}

static void u(char *tag, char *s, int base) {
  char *e;
  unsigned long v;
  e = 0;
  v = strtoul(s, &e, base);
  printf("%s %lu @%d\n", tag, v, (int)(e - s));
}

int main(void) {
  char *e;
  char *s;
  long long q;
  unsigned long long uq;
  div_t d;

  l("dec   ", "42", 10);
  l("space ", "  \t -17xyz", 10);
  l("plus  ", "+9", 10);
  l("hex   ", "+0x1f", 16);
  l("hexb0 ", "0x1f", 0);
  l("hexd  ", "0x1f", 10);        /* 10 進では "0" までしか読めない */
  l("hexbar", "0x", 16);          /* 主部は "0"。endptr は x の手前 */
  l("oct0  ", "011", 0);
  l("oct8  ", "011", 8);
  l("dec0  ", "011", 10);
  l("b36   ", "z", 36);
  l("b36b  ", "ZZ", 36);
  l("b2    ", "0b11", 2);         /* "0" までしか読めない */
  l("empty ", "", 10);            /* endptr は先頭のまま */
  l("blank ", "   ", 10);
  l("sign  ", "-", 10);
  l("bad   ", "xyz", 10);
  l("caps  ", "0XAb", 16);

  l("wsall ", "\n\v\f\r \t9", 10);   /* C89 の空白は 6 種類ある */

  u("udec  ", "1234567", 10);
  u("uhex  ", "0xffff", 0);
  u("ublank", "  +7", 10);
  u("uwsall", "\n\v\f\r \t9", 10);
  u("ubar  ", "0x", 16);            /* 主部は "0"。x は読まない */
  u("unone ", "  xyz", 10);         /* 変換できなければ endptr は先頭 */
  u("usign ", "  -", 10);           /* 符号だけでも変換ではない */

  /* 64 bit の側は幅がホストと同じなので，値そのものを比べられる。
   * **1 つの printf の中で変換と e - s を並べない** —— 評価の順序を
   * C は定めないので，並べると意味が変わる */
  s = "-1234567890123";
  e = 0;
  q = strtoll(s, &e, 10);
  printf("ll     %lld @%d\n", q, (int)(e - s));
  s = "18446744073709551615";
  e = 0;
  uq = strtoull(s, &e, 10);
  printf("ull    %llu @%d\n", uq, (int)(e - s));

  printf("atoi   %d %d %d\n", atoi("  -42abc"), atoi("+7"), atoi("x"));
  printf("abs    %d %d %d\n", abs(-5), abs(0), abs(5));
  d = div(7, 2);
  printf("div    %d %d\n", d.quot, d.rem);
  d = div(-7, 2);
  printf("divneg %d %d\n", d.quot, d.rem);
  d = div(7, -2);
  printf("divnd  %d %d\n", d.quot, d.rem);
  return 0;
}
