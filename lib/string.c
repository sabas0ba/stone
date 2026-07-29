/* string.c --- 文字列と記憶域の操作 (C89 7.11)
 *
 * 設計は docs/stage011-libc.md 3.2。要点:
 *   - 比較は unsigned char として行う (C89 の要求)。この処理系の素の
 *     char は符号なしなので実害は出ないが，仕様どおりに書いておく
 *   - 速度のための語単位の複写は入れない。整列していない領域の扱いが
 *     増え，得られるものに対して複雑すぎる
 *   - strspn / strcspn / strpbrk は文字集合を 256 バイトの表に展開する
 */
#include <stddef.h>
#include <string.h>

/* 領域が重なる場合の動作は未定義 (C89 どおり) */
void *memcpy(void *d, void *s, size_t n) {
  char *dp;
  char *sp;
  dp = (char *)d;
  sp = (char *)s;
  while (n > 0) { *dp = *sp; dp++; sp++; n--; }
  return d;
}

/* 重なりを見て前から / 後ろからを選ぶ。d == s なら何も複写しない */
void *memmove(void *d, void *s, size_t n) {
  char *dp;
  char *sp;
  dp = (char *)d;
  sp = (char *)s;
  if (dp < sp) {
    while (n > 0) { *dp = *sp; dp++; sp++; n--; }
  } else if (dp > sp) {
    dp += n;
    sp += n;
    while (n > 0) { dp--; sp--; *dp = *sp; n--; }
  }
  return d;
}

void *memset(void *d, int c, size_t n) {
  char *dp;
  dp = (char *)d;
  while (n > 0) { *dp = (char)c; dp++; n--; }
  return d;
}

int memcmp(void *a, void *b, size_t n) {
  unsigned char *ap;
  unsigned char *bp;
  ap = (unsigned char *)a;
  bp = (unsigned char *)b;
  while (n > 0) {
    if (*ap != *bp) return *ap - *bp;
    ap++;
    bp++;
    n--;
  }
  return 0;
}

size_t strlen(char *s) {
  size_t n;
  n = 0;
  while (s[n]) n++;
  return n;
}

char *strcpy(char *d, char *s) {
  char *p;
  p = d;
  while (*s) { *p = *s; p++; s++; }
  *p = 0;
  return d;
}

/* n に満たない分は NUL で埋める (C89 どおり)。n 以内に NUL が
 * 無ければ終端しない (これも C89 どおり) */
char *strncpy(char *d, char *s, size_t n) {
  size_t i;
  i = 0;
  while (i < n && s[i]) { d[i] = s[i]; i++; }
  while (i < n) { d[i] = 0; i++; }
  return d;
}

int strcmp(char *a, char *b) {
  unsigned char *ap;
  unsigned char *bp;
  ap = (unsigned char *)a;
  bp = (unsigned char *)b;
  while (*ap && *ap == *bp) { ap++; bp++; }
  return *ap - *bp;
}

int strncmp(char *a, char *b, size_t n) {
  unsigned char *ap;
  unsigned char *bp;
  size_t i;
  ap = (unsigned char *)a;
  bp = (unsigned char *)b;
  i = 0;
  while (i < n) {
    if (ap[i] != bp[i]) return ap[i] - bp[i];
    if (ap[i] == 0) return 0;
    i++;
  }
  return 0;
}

/* 終端の NUL も探索対象に含む (C89 どおり)。strchr(s, 0) は終端を指す */
char *strchr(char *s, int c) {
  char ch;
  ch = (char)c;
  while (*s) {
    if (*s == ch) return s;
    s++;
  }
  if (ch == 0) return s;
  return NULL;
}

char *strrchr(char *s, int c) {
  char *r;
  char ch;
  ch = (char)c;
  r = NULL;
  while (*s) {
    if (*s == ch) r = s;
    s++;
  }
  if (ch == 0) return s;
  return r;
}

char *strcat(char *d, char *s) {
  char *p;
  p = d;
  while (*p) p++;
  while (*s) { *p = *s; p++; s++; }
  *p = 0;
  return d;
}

/* s から高々 n バイトを写し，必ず NUL で終端する (C89 どおり) */
char *strncat(char *d, char *s, size_t n) {
  char *p;
  size_t i;
  p = d;
  while (*p) p++;
  i = 0;
  while (i < n && s[i]) { *p = s[i]; p++; i++; }
  *p = 0;
  return d;
}

/* 素朴な二重走査。空の針は先頭に一致する (C89 どおり) */
char *strstr(char *h, char *n) {
  size_t i;
  if (*n == 0) return h;
  while (*h) {
    i = 0;
    while (h[i] && n[i] && h[i] == n[i]) i++;
    if (n[i] == 0) return h;
    h++;
  }
  return NULL;
}

/* 先頭から，集合の文字だけが続く長さ。NUL は表に立てないので必ず止まる */
size_t strspn(char *s, char *set) {
  char tbl[256];
  int i;
  size_t n;
  i = 0;
  while (i < 256) { tbl[i] = 0; i++; }
  while (*set) { tbl[*set] = 1; set++; }
  n = 0;
  while (tbl[s[n]]) n++;
  return n;
}

/* 先頭から，集合の文字が現れない長さ。NUL を表に立てて終端で止める */
size_t strcspn(char *s, char *set) {
  char tbl[256];
  int i;
  size_t n;
  i = 0;
  while (i < 256) { tbl[i] = 0; i++; }
  tbl[0] = 1;
  while (*set) { tbl[*set] = 1; set++; }
  n = 0;
  while (!tbl[s[n]]) n++;
  return n;
}

char *strpbrk(char *s, char *set) {
  char tbl[256];
  int i;
  i = 0;
  while (i < 256) { tbl[i] = 0; i++; }
  while (*set) { tbl[*set] = 1; set++; }
  while (*s) {
    if (tbl[*s]) return s;
    s++;
  }
  return NULL;
}
