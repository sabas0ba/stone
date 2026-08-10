/* stdio.c --- 入出力 (C89 7.9)
 *
 * 設計は docs/stage012-os.md 6.4。libc の環境部であり，read / write /
 * open / close (lib/posix/sys.c) の上に立つ。
 *
 * バッファリングはしないので，fflush は何もしない。書式は %d %u %x %c %s
 * %% と最小の幅指定 (0 詰めを含む) だけを実装する。
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define NFILE 16

static FILE files[NFILE];
static int inited;

/* 標準の 3 本は fd 0 / 1 / 2 に結ぶ。初期値つきの大域構造体を使わず，
 * 最初の呼出しで組み立てる (.bss は 0 で始まるので inited が印になる) */
FILE *__stdfile(int i) {
  int k;
  if (!inited) {
    inited = 1;
    for (k = 0; k < NFILE; k++) {
      files[k].fd = -1;
      files[k].back = -1;
    }
    files[0].fd = 0;
    files[1].fd = 1;
    files[2].fd = 2;
  }
  return &files[i];
}

FILE *fopen(char *path, char *mode) {
  FILE *f;
  int k;
  int fd;
  int flags;

  __stdfile(0);                         /* 表の初期化を済ませる */
  if (mode[0] == 'r') flags = O_RDONLY;
  else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT;
  else return NULL;
  fd = open(path, flags);
  if (fd < 0) return NULL;
  for (k = 3; k < NFILE; k++) {
    if (files[k].fd < 0) {
      f = &files[k];
      f->fd = fd;
      f->back = -1;
      f->eof = 0;
      f->err = 0;
      return f;
    }
  }
  close(fd);
  return NULL;
}

int fclose(FILE *f) {
  int r;
  if (f == NULL || f->fd < 0) return EOF;
  r = close(f->fd);
  f->fd = -1;
  return r;
}

int fgetc(FILE *f) {
  char c;
  int n;

  if (f->back >= 0) {
    n = f->back;
    f->back = -1;
    return n;
  }
  n = read(f->fd, &c, 1);
  if (n < 0) { f->err = 1; return EOF; }
  if (n == 0) { f->eof = 1; return EOF; }
  return c & 255;
}

int fputc(int c, FILE *f) {
  char b;
  b = c;
  if (write(f->fd, &b, 1) != 1) { f->err = 1; return EOF; }
  return c & 255;
}

/* 押し戻せるのは 1 バイトまで (C89 が保証するのもそこまで) */
int ungetc(int c, FILE *f) {
  if (c == EOF || f->back >= 0) return EOF;
  f->back = c & 255;
  f->eof = 0;
  return c & 255;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f) {
  size_t i;
  size_t tot;
  int c;
  char *p;

  p = (char *)buf;
  tot = size * n;
  for (i = 0; i < tot; i++) {
    c = fgetc(f);
    if (c == EOF) break;
    p[i] = c;
  }
  if (size == 0) return 0;
  return i / size;
}

size_t fwrite(void *buf, size_t size, size_t n, FILE *f) {
  size_t tot;
  int w;

  tot = size * n;
  if (tot == 0) return 0;
  w = write(f->fd, buf, tot);
  if (w < 0) { f->err = 1; return 0; }
  if (size == 0) return 0;
  return (size_t)w / size;
}

/* 改行まで (改行を含む) 読み，NUL で終端する。1 バイトも読めなければ NULL */
char *fgets(char *s, int n, FILE *f) {
  int i;
  int c;

  if (n <= 0) return NULL;
  i = 0;
  while (i < n - 1) {
    c = fgetc(f);
    if (c == EOF) break;
    s[i] = c;
    i = i + 1;
    if (c == '\n') break;
  }
  if (i == 0) return NULL;
  s[i] = 0;
  return s;
}

int fputs(char *s, FILE *f) {
  int n;
  n = 0;
  while (s[n]) n = n + 1;
  if (n == 0) return 0;
  if (write(f->fd, s, n) != n) { f->err = 1; return EOF; }
  return n;
}

int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }

/* 無バッファなので溜まっているものは無い */
int fflush(FILE *f) { return 0; }

int getchar(void) { return fgetc(stdin); }
int putchar(int c) { return fputc(c, stdout); }

int puts(char *s) {
  if (fputs(s, stdout) == EOF) return EOF;
  return fputc('\n', stdout);
}

/* ---- 書式出力 ---- */

/* 符号なしを基数 base で書く。幅 w に満たなければ pad で詰める */
static int pnum(FILE *f, unsigned v, unsigned base, int w, int pad) {
  char b[16];
  int n;
  int i;
  int d;
  int dn;

  n = 0;
  if (v == 0) { b[0] = '0'; n = 1; }
  while (v > 0) {
    d = (int)(v % base);
    b[n] = d < 10 ? '0' + d : 'a' + d - 10;
    n = n + 1;
    v = v / base;
  }
  dn = n;
  i = 0;
  while (n + i < w) { emitc(f, pad); i = i + 1; }
  while (n > 0) { n = n - 1; emitc(f, b[n]); }
  return i + dn;
}

/* sprintf の書込み先。NULL でなければ FILE ではなくここへ書く */
static char *cap;

static int emitc(FILE *f, int c) {
  if (cap != NULL) {
    *cap = (char)c;
    cap = cap + 1;
    return c;
  }
  return fputc(c, f);
}

/* 実装する変換は %d %u %x %c %s %% と，幅 (0 詰め・- 左詰め)，
 * l 修飾 (long は int と同じ幅なので読み捨てる) */
static int vfpr(FILE *f, char *fmt, va_list ap) {
  int i;
  int w;
  int pad;
  int left;
  int v;
  char *s;
  int cnt;
  int n;
  int k;

  cnt = 0;
  i = 0;
  while (fmt[i]) {
    if (fmt[i] != '%') { emitc(f, fmt[i]); cnt = cnt + 1; i = i + 1; continue; }
    i = i + 1;
    left = 0;
    pad = ' ';
    while (fmt[i] == '-' || fmt[i] == '0') {
      if (fmt[i] == '-') left = 1;
      else pad = '0';
      i = i + 1;
    }
    w = 0;
    while (fmt[i] >= '0' && fmt[i] <= '9') { w = w * 10 + (fmt[i] - '0'); i = i + 1; }
    /* long は int と同じ幅なので l は読み捨てる (%ld %lu %lx) */
    while (fmt[i] == 'l') i = i + 1;
    if (fmt[i] == 'd') {
      v = va_arg(ap, int);
      n = 0;
      if (v < 0) {
        emitc(f, '-');
        /* INT_MIN は符号を反転できないので unsigned のまま扱う */
        n = 1 + pnum(f, (unsigned)(0 - (unsigned)v), 10, left ? 0 : w - 1, pad);
      } else {
        n = pnum(f, (unsigned)v, 10, left ? 0 : w, pad);
      }
      while (n < w) { emitc(f, ' '); n = n + 1; }
      cnt = cnt + n;
    } else if (fmt[i] == 'u' || fmt[i] == 'x') {
      k = 10;
      if (fmt[i] == 'x') k = 16;
      n = pnum(f, (unsigned)va_arg(ap, int), (unsigned)k, left ? 0 : w, pad);
      while (n < w) { emitc(f, ' '); n = n + 1; }
      cnt = cnt + n;
    } else if (fmt[i] == 'c') {
      emitc(f, va_arg(ap, int));
      cnt = cnt + 1;
    } else if (fmt[i] == 's') {
      s = va_arg(ap, char *);
      n = 0;
      while (s[n]) n = n + 1;
      k = 0;
      if (!left) { while (n + k < w) { emitc(f, ' '); k = k + 1; } }
      v = 0;
      while (s[v]) { emitc(f, s[v]); v = v + 1; }
      if (left) { while (n + k < w) { emitc(f, ' '); k = k + 1; } }
      cnt = cnt + n + k;
    } else if (fmt[i] == '%') {
      emitc(f, '%');
      cnt = cnt + 1;
    } else {
      emitc(f, fmt[i]);
      cnt = cnt + 1;
    }
    i = i + 1;
  }
  return cnt;
}

int vfprintf(FILE *f, char *fmt, va_list ap) {
  return vfpr(f, fmt, ap);
}

int vsprintf(char *buf, char *fmt, va_list ap) {
  int n;
  cap = buf;
  n = vfpr(NULL, fmt, ap);
  *cap = 0;
  cap = NULL;
  return n;
}

int sprintf(char *buf, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}

int fprintf(FILE *f, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfpr(f, fmt, ap);
  va_end(ap);
  return n;
}

int printf(char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vfpr(stdout, fmt, ap);
  va_end(ap);
  return n;
}
