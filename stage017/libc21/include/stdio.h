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
 * 非目標: setvbuf，scanf 系，%f (浮動小数点)。
 *
 * ---- 追記 ("a") について (第 21 世代) ----
 *
 * 第 20 世代までの `fopen(path, "a")` は **O_WRONLY | O_CREAT で開く
 * だけ**だった。カーネルは記述子の位置を 0 から始めるので，**既存の
 * ファイルの先頭から上書きする**。stage014/libc.md の「制限」に
 * そう書いてあり，そこには「seek 系の syscall がそもそも無いので
 * 逃げ道も無い」ともあった。
 *
 * **その前提はもう成り立たない。** lseek (62) が入っている
 * (docs/stage017-cc.md 11 章)。そこで `app` を立て，**書く前に必ず
 * 末尾へ寄せる**形で追記を libc の側に実装した。我々は単一の走行なので
 * (spawn は子の終わりを待つ)，これで POSIX の O_APPEND と同じ意味に
 * なる —— 途中で fseek しても、次に書くのは末尾である。
 *
 * **生の O_APPEND は open() が拒む。** カーネルが知らない旗を黙って
 * 捨てるためで、こちらは実装していない (fcntl.h の註)。
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
  int app;                      /* 追記の流れ (第 21 世代)。下の註 */
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
int snprintf(char *buf, size_t size, char *fmt, ...);
int vsnprintf(char *buf, size_t size, char *fmt, va_list ap);
int sscanf(char *s, char *fmt, ...);
long ftell(FILE *f);
int fseek(FILE *f, long off, int whence);
FILE *fdopen(int fd, char *mode);
int remove(char *path);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int sprintf(char *buf, char *fmt, ...);
int vfprintf(FILE *f, char *fmt, va_list ap);
int vsprintf(char *buf, char *fmt, va_list ap);
int fprintf(FILE *f, char *fmt, ...);

#endif
