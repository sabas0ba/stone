/* limits.h --- 整数型の範囲 (C89 5.2.4.2.1)
 *
 * 幅と符号は docs/stage010-c89.md 12 章のとおり:
 *   char は 1 バイトで符号なし (処理系定義)。CHAR_MIN / CHAR_MAX が
 *   0 / 255 なのはそのためである。
 *   long は int と同じ 4 バイト。
 *
 * INT_MIN を -2147483648 と書かないのは，C の字句では「2147483648 に
 * 単項マイナス」であり，正の側が int に収まらないからである。
 * INT_MAX から 1 を引いた式で定義する。
 *
 * このコンパイラの整数リテラルに u / l の接尾辞は無いので，
 * UINT_MAX / ULONG_MAX は 16 進のビットパターンで書く。
 */
#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT 8

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

#define CHAR_MIN 0
#define CHAR_MAX 255

#define MB_LEN_MAX 1

#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535

#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 0xffffffff

#define LONG_MIN (-2147483647 - 1)
#define LONG_MAX 2147483647
#define ULONG_MAX 0xffffffff

#endif
