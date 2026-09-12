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

unsigned long strtoul(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);
long long strtoll(const char *s, char **endptr, int base);
long long atoll(char *s);
double strtod(const char *s, char **endptr);
float strtof(const char *s, char **endptr);
/* long double は double と同じ 8 バイトである (cc も tcc の RV32 も)。
 * ただし tcc は型としては別に数えるので，マクロで strtod に潰すと
 * tcc.h の extern 宣言と型が食い違う。実体のある関数として持つ */
long double strtold(const char *s, char **endptr);
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(void *, void *));
void *bsearch(void *key, void *base, size_t nmemb, size_t size,
              int (*cmp)(void *, void *));
long strtol(const char *s, char **endptr, int base);
int atoi(char *s);
int abs(int n);

/* 経路を絶対形に直し . と .. を畳む (POSIX。第 17 世代)。
 * resolved が NULL なら malloc して返す (GNU の拡張。tcc が使う)。
 * シンボリックリンクが無いので字句的な畳み込みで足りる */
char *realpath(char *path, char *resolved);
div_t div(int numer, int denom);

/* exit の実体は libc ではなく実行環境の側にある ('E' 前置部，あるいは
 * tcc の ABI で組むときは tccrt/start.S)。宣言だけここに置くのは，
 * 暗黙の宣言を errorにする処理系 (tcc) で libc 自身を翻訳するため
 * である (docs/stage015-tcc.md 12.17)。 */
void exit(int status);

#endif
