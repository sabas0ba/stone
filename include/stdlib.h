/* stdlib.h --- 汎用ユーティリティのうち記憶域の管理 (C89 7.10.3)
 *
 * 実装は lib/stdlib.c。方針は docs/stage011-libc.md 7 章。
 *
 * 第 2 部で宣言するのは malloc / free / calloc / realloc のみである。
 * 整列と探索・数値変換 (qsort / bsearch / atoi / strtol / abs / div) は
 * 第 3 部で足す。exit / abort / atexit は実行環境に依存するため
 * Stage 12 の課題である。
 *
 * const を付けない理由は string.h と同じ (docs/stage011-libc.md 3.4)。
 */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void free(void *p);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);

#endif
