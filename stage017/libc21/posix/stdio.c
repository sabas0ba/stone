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
      files[k].app = 0;
    }
    files[0].fd = 0;
    files[1].fd = 1;
    files[2].fd = 2;
  }
  return &files[i];
}

/* 追記の流れは**書く前に必ず末尾へ寄せる** (第 21 世代)。
 *
 * カーネルに O_APPEND が無いので libc の側でやる。単一の走行なので
 * (spawn は子の終わりを待つ)，これで POSIX の O_APPEND と同じ意味に
 * なる。**寄せられなければ書かない** —— 書くと先頭を潰すからである
 * (docs/stage017-cc.md 32 章)。
 */
static int wr(FILE *f, void *buf, int n) {
  if (f->app && lseek(f->fd, 0, SEEK_END) < 0) {
    f->err = 1;
    return -1;
  }
  return write(f->fd, buf, n);
}

FILE *fopen(char *path, char *mode) {
  FILE *f;
  int k;
  int fd;
  int flags;
  int app;

  __stdfile(0);                         /* 表の初期化を済ませる */
  app = 0;
  if (mode[0] == 'r') flags = O_RDONLY;
  else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
  /* **O_APPEND は渡さない。** カーネルが知らない旗を黙って捨てるので，
   * 渡しても効かない —— そして open() はそれを拒む (fcntl.h の註)。
   * 追記はここ (libc) で実装する。印だけ立てて，書く前に末尾へ寄せる */
  else if (mode[0] == 'a') { flags = O_WRONLY | O_CREAT; app = 1; }
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
      f->app = app;
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
  if (wr(f, &b, 1) != 1) { f->err = 1; return EOF; }
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
  w = wr(f, buf, tot);
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
  if (wr(f, s, n) != n) { f->err = 1; return EOF; }
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

/* 実体は下 (sprintf の書込み先の宣言と一緒に置きたいため)。先に宣言だけ
 * するのは，暗黙の宣言を許さない処理系 (tcc) で翻訳するためである */
static int emitc(FILE *f, int c);

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
static int caplim;              /* snprintf の残り容量 (-1 = 無制限) */

static int emitc(FILE *f, int c) {
  if (cap != NULL) {
    if (caplim == 0) return c;  /* 溢れたぶんは数えるだけ (C99 の規則) */
    if (caplim > 0) caplim = caplim - 1;
    *cap = (char)c;
    cap = cap + 1;
    return c;
  }
  return fputc(c, f);
}

/* 64 bit の数を基数 k で出す (第 4 部)。pnum の 64 bit 版 */
static int pnum64(FILE *f, unsigned long long v, unsigned k, int w, int pad) {
  char tmp[24];
  int n;
  int i;
  unsigned d;
  n = 0;
  if (v == 0ULL) { tmp[n] = '0'; n = 1; }
  while (v != 0ULL) {
    d = (unsigned)(v % (unsigned long long)k);
    if (d < 10) tmp[n] = (char)('0' + d);
    else tmp[n] = (char)('a' + d - 10);
    v = v / (unsigned long long)k;
    n = n + 1;
  }
  i = n;
  while (i < w) { emitc(f, pad); i = i + 1; }
  while (n > 0) { n = n - 1; emitc(f, tmp[n]); }
  return i;
}

/* 浮動小数点を %f の形 (小数 prec 桁・四捨五入なしの切捨て寄り) で出す。
 * -bench の統計表示にしか使われないので簡素でよい (実測 11.1) */
static int pflt(FILE *f, double v, int prec) {
  int n;
  long long ip;
  double fr;
  int i;
  int d;
  n = 0;
  if (v < 0.0) { emitc(f, '-'); v = 0.0 - v; n = 1; }
  ip = (long long)v;
  n = n + pnum64(f, (unsigned long long)ip, 10, 0, ' ');
  if (prec <= 0) return n;
  emitc(f, '.');
  n = n + 1;
  fr = v - (double)ip;
  i = 0;
  while (i < prec) {
    fr = fr * 10.0;
    d = (int)fr;
    if (d > 9) d = 9;
    emitc(f, '0' + d);
    fr = fr - (double)d;
    i = i + 1;
    n = n + 1;
  }
  return n;
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
  int prec;
  int nl;
  long long lv;

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
    if (fmt[i] == '*') { w = va_arg(ap, int); i = i + 1; if (w < 0) { left = 1; w = 0 - w; } }
    else while (fmt[i] >= '0' && fmt[i] <= '9') { w = w * 10 + (fmt[i] - '0'); i = i + 1; }
    /* 精度。%s では最大長，%f では小数の桁数。数値では読み捨てる */
    prec = 0 - 1;
    if (fmt[i] == '.') {
      i = i + 1;
      prec = 0;
      if (fmt[i] == '*') { prec = va_arg(ap, int); i = i + 1; }
      else while (fmt[i] >= '0' && fmt[i] <= '9') { prec = prec * 10 + (fmt[i] - '0'); i = i + 1; }
    }
    /* l は 1 個なら int と同じ幅。2 個 (ll) は 64 bit (第 4 部) */
    nl = 0;
    while (fmt[i] == 'l') { nl = nl + 1; i = i + 1; }
    if (nl >= 2 && (fmt[i] == 'd' || fmt[i] == 'i')) {
      lv = va_arg(ap, long long);
      n = 0;
      if (lv < 0) {
        emitc(f, '-');
        n = 1 + pnum64(f, 0ULL - (unsigned long long)lv, 10, left ? 0 : w - 1, pad);
      } else {
        n = pnum64(f, (unsigned long long)lv, 10, left ? 0 : w, pad);
      }
      while (n < w) { emitc(f, ' '); n = n + 1; }
      cnt = cnt + n;
      i = i + 1;
      continue;
    }
    if (nl >= 2 && (fmt[i] == 'u' || fmt[i] == 'x' || fmt[i] == 'X')) {
      k = 10;
      if (fmt[i] != 'u') k = 16;
      n = pnum64(f, va_arg(ap, unsigned long long), (unsigned)k, left ? 0 : w, pad);
      while (n < w) { emitc(f, ' '); n = n + 1; }
      cnt = cnt + n;
      i = i + 1;
      continue;
    }
    if (fmt[i] == 'f' || fmt[i] == 'g' || fmt[i] == 'e') {
      /* 可変部の float は double へ格上げされて届く (cc15k)。
       * %g / %e も %f の形で出す (統計表示にしか使われない) */
      if (prec < 0) prec = 6;
      cnt = cnt + pflt(f, va_arg(ap, double), prec);
      i = i + 1;
      continue;
    }
    if (fmt[i] == 'd' || fmt[i] == 'i') {
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
    } else if (fmt[i] == 'u' || fmt[i] == 'x' || fmt[i] == 'X' || fmt[i] == 'p') {
      k = 10;
      if (fmt[i] != 'u') k = 16;
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
      if (prec >= 0 && n > prec) n = prec;
      k = 0;
      if (!left) { while (n + k < w) { emitc(f, ' '); k = k + 1; } }
      v = 0;
      while (v < n) { emitc(f, s[v]); v = v + 1; }
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
  caplim = 0 - 1;
  n = vfpr(NULL, fmt, ap);
  *cap = 0;
  cap = NULL;
  return n;
}

/* n には終端の 0 を含む (C99 の snprintf の規則)。返り値は
 * 「入り切ったとしたら書いた長さ」で，切り詰めの検出に使える */
int vsnprintf(char *buf, size_t size, char *fmt, va_list ap) {
  int n;
  if (size == 0) {
    static char sink;
    int m;
    cap = &sink;                /* 書かずに数えるだけ (caplim = 0) */
    caplim = 0;
    m = vfpr(NULL, fmt, ap);
    cap = NULL;
    caplim = 0 - 1;
    return m;
  }
  cap = buf;
  caplim = (int)size - 1;
  n = vfpr(NULL, fmt, ap);
  *cap = 0;
  cap = NULL;
  caplim = 0 - 1;
  return n;
}

int snprintf(char *buf, size_t size, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
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

/* ---- 第 4 部: 位置つきの入出力 ---- */

/* lseek の宣言はここに置いていたが，**どのヘッダにも無かった**ので
 * 読む側 (zlib) が暗黙の int 宣言になっていた。第 21 世代で unistd.h に
 * 移した (docs/stage017-cc.md 32.3) */

long ftell(FILE *f)
{
    long p;
    p = lseek(f->fd, 0, SEEK_CUR);
    if (p < 0)
        return p;
    if (f->back >= 0)
        return p - 1;           /* 押し戻した 1 文字ぶん手前にいる */
    return p;
}

int fseek(FILE *f, long off, int whence)
{
    f->eof = 0;
    f->back = -1;               /* ungetc の押し戻しは捨てる */
    if (lseek(f->fd, off, whence) < 0)
        return 0 - 1;
    return 0;
}

/* 既に開いている fd を FILE で包む。fopen と同じ表から空きを取る */
FILE *fdopen(int fd, char *mode)
{
    int k;
    __stdfile(0);
    if (fd < 0)
        return NULL;
    /* 0 / 1 / 2 は UART で，位置を持たない。**追記の印は立てない** ——
     * 立てると書くたびに lseek が失敗する (第 21 世代) */
    if (fd < 3)
        return &files[fd];
    for (k = 3; k < NFILE; k++) {
        if (files[k].fd < 0) {
            files[k].fd = fd;
            files[k].back = -1;
            files[k].eof = 0;
            files[k].err = 0;
            /* **印は mode で決める。** fclose は fd を -1 にするだけ
             * なので，追記の流れが閉じた枠には app = 1 が残る。
             * 落とさないと次にこの枠を取った流れが末尾へ寄せてしまう。
             * かといって落とすだけだと fdopen(fd, "a") が追記に
             * ならない —— **どちらも黙って誤る形である** (第 21 世代) */
            files[k].app = (mode != 0 && mode[0] == 'a');
            return &files[k];
        }
    }
    return NULL;
}
