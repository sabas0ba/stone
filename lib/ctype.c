/* ctype.c --- 文字の分類と変換 (C89 7.3)
 *
 * 設計は docs/stage011-libc.md 3.3。表 (256 バイト) ではなく範囲比較で
 * 書く。表を持つと使わなくても領域を消費するが，範囲比較なら分岐数個で
 * 済み，どの関数も独立にリンクできる。
 *
 * 引数は unsigned char の値か EOF (-1)。EOF を渡しても誤動作しないよう，
 * 範囲比較では必ず下限も検査する (iscntrl の c >= 0 がそれである)。
 *
 * ロケールは持たないので，分類は ASCII に固定である。
 */
#include <ctype.h>

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }

int isxdigit(int c) {
  if (isdigit(c)) return 1;
  if (c >= 'a' && c <= 'f') return 1;
  return c >= 'A' && c <= 'F';
}

int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

int iscntrl(int c) { return (c >= 0 && c <= 31) || c == 127; }
int isprint(int c) { return c >= ' ' && c <= '~'; }
int isgraph(int c) { return c > ' ' && c <= '~'; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }

/* 大文字と小文字の距離は ASCII で 32 ('a' - 'A') */
int tolower(int c) {
  if (isupper(c)) return c + ('a' - 'A');
  return c;
}

int toupper(int c) {
  if (islower(c)) return c - ('a' - 'A');
  return c;
}
