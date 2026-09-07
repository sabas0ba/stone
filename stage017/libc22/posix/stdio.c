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

/* 整数を 1 つ書き出す (第 22 世代。第 21 世代までの pnum / pnum64 を
 * 1 つにまとめ，精度と旗を持たせたもの)。
 *
 * **旗と精度を足したのは，ホストと突き合わせて出た穴である**
 * (docs/stage017-gcc.md 5.3)。第 21 世代は `%o` を知らず，`%X` を
 * 小文字で書き，`%.3d` の精度を読み捨て，`%+d` `% d` `%#x` の旗に
 * 至っては**可変部を 1 つも取り出さないまま次の変換へ進んで**いた。
 *
 *   v     値 (符号は sgn が持つ)
 *   base  2〜16
 *   up    大文字で書く (%X)
 *   sgn   先に出す符号の文字 ('-' / '+' / ' ')。0 なら出さない
 *   alt   # 旗
 *   prec  精度。-1 は指定なし。0 で値が 0 なら**桁を 1 つも書かない**
 *   w     欄の幅   pad0  0 で詰める   left  左詰め
 *
 * 返り値は実際に書いた文字数である。 */
static int pout(FILE *f, unsigned long long v, unsigned base, int up,
                int sgn, int alt, int prec, int w, int pad0, int left) {
  char b[24];
  int n;
  int i;
  int d;
  int zeros;
  int pfx;
  int len;

  n = 0;
  if (v == 0ULL) {
    /* 精度 0 の 0 は空である (C89 7.9.6.1)。ただし # 旗つきの 8 進は
     * 下で 0 を 1 つ足す */
    if (prec != 0) { b[0] = '0'; n = 1; }
  }
  while (v != 0ULL) {
    d = (int)(v % (unsigned long long)base);
    if (d < 10) b[n] = (char)('0' + d);
    else b[n] = (char)((up ? 'A' : 'a') + d - 10);
    v = v / (unsigned long long)base;
    n = n + 1;
  }

  zeros = 0;
  if (prec > n) zeros = prec - n;
  pfx = 0;
  if (alt && base == 16 && n > 0) pfx = 2;     /* 0x / 0X。0 には付けない */
  if (alt && base == 8 && zeros == 0 && (n == 0 || b[n - 1] != '0'))
    zeros = 1;                                 /* 先頭を 0 にする */

  len = n + zeros + pfx;
  if (sgn) len = len + 1;

  /* 空白詰めは符号より前，0 詰めは符号より後ろ。**精度が指定された
   * 整数変換では 0 旗は効かない** (C89 7.9.6.1) */
  if (left || prec >= 0) pad0 = 0;
  i = len;
  if (!left && !pad0) { while (i < w) { emitc(f, ' '); i = i + 1; } }
  if (sgn) emitc(f, sgn);
  if (pfx) { emitc(f, '0'); emitc(f, up ? 'X' : 'x'); }
  if (pad0) { while (i < w) { emitc(f, '0'); i = i + 1; } }
  while (zeros > 0) { emitc(f, '0'); zeros = zeros - 1; }
  while (n > 0) { n = n - 1; emitc(f, b[n]); }
  if (left) { while (i < w) { emitc(f, ' '); i = i + 1; } }
  return i;
}

/* 浮動小数点を %f の形で出す。
 *
 * **第 21 世代は切り捨てていた** —— `%.0f` の 2.6 が 2 になる。C89
 * 7.9.6.1 は「丸める」と定めているので，これは誤りである
 * (docs/stage017-gcc.md 5.3)。書く桁の 1 つ下に 5 を足してから
 * 切り捨てる形で，繰り上がりは整数部まで伝わる。
 *
 * ちょうど半分の値をどちらへ倒すかは C が定めていない。我々は 0 から
 * 遠い側で，ホスト (glibc) は偶数側である (stage017/libc22.md 4)。 */
static int pflt(FILE *f, double v, int prec) {
  int n;
  long long ip;
  double fr;
  double half;
  int i;
  int d;
  n = 0;
  if (v < 0.0) { emitc(f, '-'); v = 0.0 - v; n = 1; }
  half = 0.5;
  i = 0;
  while (i < prec) { half = half / 10.0; i = i + 1; }
  v = v + half;
  ip = (long long)v;
  n = n + pout(f, (unsigned long long)ip, 10, 0, 0, 0, 0 - 1, 0, 0, 0);
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

/* 実装する変換は %d %i %u %o %x %X %c %s %p %% と，旗 (- 0 + 空白 #)，
 * 幅，精度，長さ修飾 (h は int へ格上げされて届くので読み捨て，l は
 * long == int なので同じ，ll は 64 bit)。
 *
 * **第 21 世代との差はすべて，ホストと突き合わせて出たものである**
 * (docs/stage017-gcc.md 5.3)。とくに旗は，知らない文字を「普通の字」
 * として書き出していたので，`%+d` が "+d" になったうえ**可変部を
 * 取り出さないまま次へ進み**，同じ printf の残りの引数がすべてずれた。 */
static int vfpr(FILE *f, char *fmt, va_list ap) {
  int i;
  int w;
  int pad0;
  int left;
  int plus;
  int space;
  int alt;
  int sgn;
  int v;
  char *s;
  int cnt;
  int n;
  int k;
  int up;
  int prec;
  int nl;
  int c;
  long long lv;
  unsigned long long uv;

  cnt = 0;
  i = 0;
  while (fmt[i]) {
    if (fmt[i] != '%') { emitc(f, fmt[i]); cnt = cnt + 1; i = i + 1; continue; }
    i = i + 1;
    left = 0;
    pad0 = 0;
    plus = 0;
    space = 0;
    alt = 0;
    while (fmt[i] == '-' || fmt[i] == '0' || fmt[i] == '+'
           || fmt[i] == ' ' || fmt[i] == '#') {
      if (fmt[i] == '-') left = 1;
      else if (fmt[i] == '0') pad0 = 1;
      else if (fmt[i] == '+') plus = 1;
      else if (fmt[i] == ' ') space = 1;
      else alt = 1;
      i = i + 1;
    }
    w = 0;
    if (fmt[i] == '*') { w = va_arg(ap, int); i = i + 1; if (w < 0) { left = 1; w = 0 - w; } }
    else while (fmt[i] >= '0' && fmt[i] <= '9') { w = w * 10 + (fmt[i] - '0'); i = i + 1; }
    /* 精度。%s では最大長，%f では小数の桁数，整数では最小の桁数 */
    prec = 0 - 1;
    if (fmt[i] == '.') {
      i = i + 1;
      prec = 0;
      if (fmt[i] == '*') { prec = va_arg(ap, int); i = i + 1; }
      else while (fmt[i] >= '0' && fmt[i] <= '9') { prec = prec * 10 + (fmt[i] - '0'); i = i + 1; }
      if (prec < 0) prec = 0 - 1;   /* * が負なら「指定なし」と同じ */
    }
    /* l は 1 個なら int と同じ幅。2 個 (ll) は 64 bit (第 4 部)。
     * h / hh は既定の格上げで int になって届くので読み捨てる */
    nl = 0;
    while (fmt[i] == 'l') { nl = nl + 1; i = i + 1; }
    while (fmt[i] == 'h') i = i + 1;
    c = fmt[i];

    /* 符号つき整数 */
    if (c == 'd' || c == 'i') {
      if (nl >= 2) lv = va_arg(ap, long long);
      else lv = (long long)va_arg(ap, int);
      sgn = 0;
      if (lv < 0) sgn = '-';
      else if (plus) sgn = '+';
      else if (space) sgn = ' ';
      /* 最小値は符号を反転できないので符号なしのまま扱う */
      if (lv < 0) uv = 0ULL - (unsigned long long)lv;
      else uv = (unsigned long long)lv;
      cnt = cnt + pout(f, uv, 10, 0, sgn, 0, prec, w, pad0, left);
      i = i + 1;
      continue;
    }
    /* 符号なし整数。+ と空白の旗は符号つきにしか効かない (C89) */
    if (c == 'u' || c == 'o' || c == 'x' || c == 'X' || c == 'p') {
      k = 10;
      up = 0;
      if (c == 'o') k = 8;
      else if (c != 'u') k = 16;
      if (c == 'X') up = 1;
      if (nl >= 2) uv = va_arg(ap, unsigned long long);
      else uv = (unsigned long long)va_arg(ap, unsigned);
      cnt = cnt + pout(f, uv, (unsigned)k, up, 0, c == 'u' ? 0 : alt,
                       prec, w, pad0, left);
      i = i + 1;
      continue;
    }
    if (c == 'f' || c == 'g' || c == 'e') {
      /* 可変部の float は double へ格上げされて届く (cc15k)。
       * %g / %e も %f の形で出す (統計表示にしか使われない) */
      if (prec < 0) prec = 6;
      cnt = cnt + pflt(f, va_arg(ap, double), prec);
      i = i + 1;
      continue;
    }
    if (fmt[i] == 'c') {
      /* %c も欄の幅を持てる (C89 7.9.6.1)。%s と同じ扱いにする */
      k = 0;
      if (!left) { while (1 + k < w) { emitc(f, ' '); k = k + 1; } }
      emitc(f, va_arg(ap, int));
      if (left) { while (1 + k < w) { emitc(f, ' '); k = k + 1; } }
      cnt = cnt + 1 + k;
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
