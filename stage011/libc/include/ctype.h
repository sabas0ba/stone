/* ctype.h --- 文字の分類と変換 (C89 7.3)
 *
 * 実装は lib/ctype.c。方針は docs/stage011-libc.md 3.3。
 *
 * どの関数も int を取り，unsigned char の値か EOF (-1) 以外を渡した
 * ときの動作は未定義である (C89 どおり)。EOF を渡しても誤動作しないよう，
 * 実装の範囲比較では必ず下限も検査する。
 *
 * ロケールは持たないので，分類は ASCII に固定である。
 */
#ifndef _CTYPE_H
#define _CTYPE_H

int isalnum(int c);
int isalpha(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

#endif
