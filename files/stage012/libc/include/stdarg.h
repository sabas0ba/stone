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
 * 進み幅に sizeof(T) ではなく 4 を使うのは，可変部に渡る値が既定の
 * 実引数拡張で int まで格上げされ，どの型でも 1 語を占めるためである。
 *
 * 制限: このコンパイラは float / double を持たないので long double も無い。
 *       va_copy は C99 のもので，C89 には無い。
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) ((ap) = __va_ptr)
#define va_arg(ap, type)   (*(type *)(((ap) += 4) - 4))
#define va_end(ap)         ((void)0)

#endif
