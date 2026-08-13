/* stdlib.h --- 汎用ユーティリティ (C89 7.10)
 *
 * 実装は lib/stdlib.c。方針は docs/stage011-libc.md 7 章 (記憶域) と
 * 8 章 (整列と探索・数値変換)。
 *
 * strtol の溢れは LONG_MAX / LONG_MIN への飽和のみで表す (errno は
 * 実行環境を得る Stage 12 まで無い)。
 *
 * 非目標: atol / labs / ldiv (long == int のため別名にすぎない)，
 *         strtoul，rand / srand。exit / abort / atexit は実行環境に
 *         依存するため Stage 12 の課題である。
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

typedef struct { int quot; int rem; } div_t;

unsigned long strtoul(char *s, char **endptr, int base);
unsigned long long strtoull(char *s, char **endptr, int base);
long long strtoll(char *s, char **endptr, int base);
long long atoll(char *s);
double strtod(char *s, char **endptr);
float strtof(char *s, char **endptr);
#define strtold strtod
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(void *, void *));
void *bsearch(void *key, void *base, size_t nmemb, size_t size,
              int (*cmp)(void *, void *));
long strtol(char *s, char **endptr, int base);
int atoi(char *s);
int abs(int n);
div_t div(int numer, int denom);

#endif
