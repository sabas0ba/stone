/* string.h --- 文字列と記憶域の操作 (C89 7.11)
 *
 * 実装は lib/string.c。方針は docs/stage011-libc.md 3.2。
 *
 * const を付けないのは，このコンパイラが const を構文として受けるだけで
 * 意味論を持たないためである。付けると「検査されている」という誤解を生む
 * (docs/stage011-libc.md 3.4)。
 *
 * 非目標: strtok (状態を持つ)，strcoll / strxfrm (ロケール)，
 *         strerror (errno が無い)，memchr (第 1 部の範囲外)。
 */
#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void *memcpy(void *d, void *s, size_t n);
void *memmove(void *d, void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(void *a, void *b, size_t n);

size_t strlen(char *s);
char *strcpy(char *d, char *s);
char *strncpy(char *d, char *s, size_t n);
int strcmp(char *a, char *b);
int strncmp(char *a, char *b, size_t n);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
char *strcat(char *d, char *s);
char *strncat(char *d, char *s, size_t n);
char *strstr(char *h, char *n);
size_t strspn(char *s, char *set);
size_t strcspn(char *s, char *set);
char *strpbrk(char *s, char *set);

/* C89 には無い (POSIX)。tcc が使うので第 4 部で足した。実体は
 * lib/misc15.c にある */
char *strdup(char *s);

#endif
