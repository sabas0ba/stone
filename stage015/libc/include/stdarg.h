/* stdarg.h --- 可変長引数 (C89 7.8)
 *
 * このコンパイラの ABI では，可変部の実引数はデータスタック上に
 * 4 バイト単位・昇順で並ぶ (docs/stage010-c89.md 16 章)。したがって
 * va_list は単なるバイトポインタでよく，va_arg は「4 進めて，進める前の
 * 位置を目的の型で読む」だけになる。
 *
 * __va_ptr は可変長引数を取る関数の中でだけコンパイラが用意する
 * 隠しローカル (char *) で，可変部の先頭を指している。
 *
 * 進み幅は型の語数で決める (libc15)。可変部の値は既定の実引数拡張で
 * int / double に格上げされ，1 語 (int 系・ポインタ) か 2 語
 * (long long / double) を占める。下位語が低い番地に来るように積まれる
 * (cc15k) ので，2 語の型はそのまま 8 バイト読みでよい。
 *
 * va_copy は C99 のものだが tcc が使う (第 6 部の実測)。va_list は
 * 単なるポインタなので代入で写せる。
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) ((ap) = __va_ptr)
#define va_arg(ap, type)   (*(type *)(((ap) += ((sizeof(type) + 3) & ~3)) - ((sizeof(type) + 3) & ~3)))
#define va_end(ap)         ((void)0)
#define va_copy(dst, src)  ((dst) = (src))

#endif
