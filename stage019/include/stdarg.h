/* stdarg.h --- Linux 5.4 を訳す処理系が持つ stdarg.h (docs/stage019-linux.md 6 章)
 *
 * Linux はこの header を処理系の include の階層から読む
 * (-isystem $(CC -print-file-name=include))。GCC と同じく，可変長引数の操作は
 * 処理系の組込み (__builtin_va_*) に任せる。我々の cc がこれらを受けるかが
 * 測定の対象の 1 つである。
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef __builtin_va_list __gnuc_va_list;
typedef __gnuc_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define va_copy(d, s)  __builtin_va_copy(d, s)

#endif
