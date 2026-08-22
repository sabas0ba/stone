/* assert.h --- 検査の主張 (C89 4.2)
 *
 * 実装は lib/posix/assert.c。成立しなければ式の文字列を標準エラーへ出して
 * exit(1) する。NDEBUG が定義されていれば何もしない式になる。
 *
 * C89 の abort ではなく exit(1) を使う (シグナルが無い。docs/stage012-os.md)。
 */
#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(e) 0
#else
#define assert(e) ((e) ? 0 : __assert(#e))
#endif

int __assert(char *s);

#endif
