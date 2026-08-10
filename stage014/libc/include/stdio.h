/* stdio.h --- 入出力 (C89 7.9)
 *
 * 実装は lib/posix/stdio.c。方針は docs/stage012-os.md 6.4。
 *
 * FILE は fd と 1 バイトの押し戻し (ungetc 用) を持つだけで，
 * **バッファリングはしない**。正しさを先に置き，速度は測ってから考える。
 *
 * stdin / stdout / stderr を関数を呼ぶマクロにしているのは，初期値つきの
 * 大域構造体を避けるためである (C89 はこれらがマクロでもよいと定める)。
 *
 * 非目標: fseek / ftell (lseek が要る)，setvbuf，scanf 系，%f (浮動小数点)。
 */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

typedef struct {
  int fd;                       /* ファイル記述子。-1 は未使用 */
  int back;                     /* 押し戻した 1 バイト。-1 は無し */
  int eof;                      /* 終端に達した */
  int err;                      /* 誤りが起きた */
} FILE;

FILE *__stdfile(int i);
#define stdin  __stdfile(0)
#define stdout __stdfile(1)
#define stderr __stdfile(2)

FILE *fopen(char *path, char *mode);
int fclose(FILE *f);
int fgetc(FILE *f);
int fputc(int c, FILE *f);
int ungetc(int c, FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(void *buf, size_t size, size_t n, FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputs(char *s, FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fflush(FILE *f);

int getchar(void);
int putchar(int c);
int puts(char *s);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int vfprintf(FILE *f, char *fmt, va_list ap);
int vsprintf(char *buf, char *fmt, va_list ap);
int fprintf(FILE *f, char *fmt, ...);

#endif
