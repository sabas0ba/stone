/* stdlib.c --- 記憶域の管理・整列と探索・数値変換 (C89 7.10)
 *
 * 記憶域の設計は docs/stage011-libc.md 7 章。要点:
 *   - ヒープの供給は morecore だけが行う。**morecore は別の翻訳単位に
 *     置き，環境ごとに差し替える** (ベアメタルは lib/morecore.c の
 *     固定領域版，OS の上では lib/posix/morecore.c の brk 版。
 *     docs/stage012-os.md 6.2)
 *   - 割付けは K&R 型のフリーリスト first-fit。各ブロックは 8 バイトの
 *     ヘッダ (次のフリーブロック，ヘッダ込みの大きさ) を持ち，free で
 *     隣接ブロックと併合する。大きさの単位はヘッダの大きさ (8 バイト)
 *   - realloc の複写と calloc の 0 埋めは内部ループで行う。memcpy や
 *     memset を呼ぶと，stdlib.o を使うすべてのプログラムが string.o の
 *     リンクを強制されるためである
 */
#include <stddef.h>
#include <stdlib.h>

struct hdr {
  struct hdr *next;             /* 次のフリーブロック (循環リスト) */
  size_t size;                  /* ヘッダ込みの大きさ (単位数) */
};

static struct hdr base;         /* フリーリストの起点 (大きさ 0 の番兵) */
static struct hdr *freep;       /* 探索の開始点。0 なら未初期化 */

/* 供給源。nu 単位以上の領域を確保して先頭を返し，実際に確保した単位数を
 * *got へ返す。失敗なら NULL。実装は環境ごとに差し替える (6.2) */
void *morecore(size_t nu, size_t *got);

void *malloc(size_t n)
{
  struct hdr *p;
  struct hdr *prev;
  struct hdr *up;
  size_t nunits;
  size_t got;

  if (n == 0) return NULL;
  nunits = (n + sizeof(struct hdr) - 1) / sizeof(struct hdr) + 1;
  prev = freep;
  if (prev == NULL) {           /* 最初の呼出し: 空リストを作る */
    base.next = &base;
    base.size = 0;
    freep = &base;
    prev = &base;
  }
  p = prev->next;
  for (;;) {
    if (p->size >= nunits) {
      if (p->size == nunits) {  /* ちょうど: リストから外す */
        prev->next = p->next;
      } else {                  /* 末尾を切り出す */
        p->size = p->size - nunits;
        p = p + p->size;
        p->size = nunits;
      }
      freep = prev;
      return (void *)(p + 1);
    }
    if (p == freep) {           /* 一周した: 補充する */
      up = (struct hdr *)morecore(nunits, &got);
      if (up == NULL) return NULL;
      up->size = got;
      free((void *)(up + 1));   /* フリーリストへ繋ぎ，隣と併合させる */
      p = freep;
    }
    prev = p;
    p = p->next;
  }
}

void free(void *ap)
{
  struct hdr *bp;
  struct hdr *p;

  if (ap == NULL) return;
  bp = (struct hdr *)ap - 1;
  /* アドレス順の挿入位置を探す。リストの端 (最大アドレスと最小アドレスの
   * 間) に入る場合はそこで止める */
  p = freep;
  while (!(bp > p && bp < p->next)) {
    if (p >= p->next && (bp > p || bp < p->next)) break;
    p = p->next;
  }
  /* 番兵 (base) は大きさ 0 の印であり，.bss 上でヒープと隣接していても
   * 併合の対象にしない */
  if (bp + bp->size == p->next && p->next != &base) {   /* 後ろと併合 */
    bp->size = bp->size + p->next->size;
    bp->next = p->next->next;
  } else {
    bp->next = p->next;
  }
  if (p + p->size == bp && p != &base) {                /* 前と併合 */
    p->size = p->size + bp->size;
    p->next = bp->next;
  } else {
    p->next = bp;
  }
  freep = p;
}

void *calloc(size_t nmemb, size_t size)
{
  size_t n;
  size_t i;
  char *p;

  if (nmemb == 0 || size == 0) return NULL;
  n = nmemb * size;
  if (n / nmemb != size) return NULL;   /* 積が size_t に収まらない */
  p = (char *)malloc(n);
  if (p == NULL) return NULL;
  for (i = 0; i < n; i++) p[i] = 0;
  return (void *)p;
}

void *realloc(void *ap, size_t n)
{
  struct hdr *h;
  size_t nunits;
  size_t old;
  size_t i;
  char *q;
  char *s;

  if (ap == NULL) return malloc(n);
  if (n == 0) {
    free(ap);
    return NULL;
  }
  h = (struct hdr *)ap - 1;
  nunits = (n + sizeof(struct hdr) - 1) / sizeof(struct hdr) + 1;
  if (h->size >= nunits) return ap;     /* 現ブロックに収まる: 在所のまま */
  q = (char *)malloc(n);
  if (q == NULL) return NULL;
  old = (h->size - 1) * sizeof(struct hdr);     /* 旧ブロックの利用者領域 */
  s = (char *)ap;
  for (i = 0; i < old; i++) q[i] = s[i];        /* old < n が保証されている */
  free(ap);
  return (void *)q;
}

/* ---- 整列と探索 (C89 7.10.5)。設計は docs/stage011-libc.md 8 章 ---- */

static void swapb(char *a, char *b, size_t size)
{
  size_t i;
  char t;

  for (i = 0; i < size; i++) { t = a[i]; a[i] = b[i]; b[i] = t; }
}

/* qsort の名前は API のものであり，実装は Shell ソートである
 * (非再帰・追加記憶域なし。8.1)。安定性は要求されない (C89 どおり) */
void qsort(void *b, size_t nmemb, size_t size, int (*cmp)(void *, void *))
{
  size_t gap;
  size_t i;
  size_t j;
  char *p;

  p = (char *)b;
  for (gap = nmemb / 2; gap > 0; gap = gap / 2)
    for (i = gap; i < nmemb; i++)
      for (j = i; j >= gap && cmp(p + (j - gap) * size, p + j * size) > 0;
           j = j - gap)
        swapb(p + (j - gap) * size, p + j * size, size);
}

/* 比較関数の第 1 引数がキー，第 2 引数が配列要素 (C89 どおり) */
void *bsearch(void *key, void *b, size_t nmemb, size_t size,
              int (*cmp)(void *, void *))
{
  size_t lo;
  size_t hi;
  size_t mid;
  int c;
  char *p;

  lo = 0;
  hi = nmemb;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    p = (char *)b + mid * size;
    c = cmp(key, (void *)p);
    if (c == 0) return (void *)p;
    if (c > 0) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

/* ---- 数値変換 (C89 7.10.1)。設計は docs/stage011-libc.md 8.3 ---- */

static int digval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return -1;
}

/* 溢れは LONG_MAX / LONG_MIN への飽和のみで表す (errno は Stage 12 まで
 * 無い)。蓄積は unsigned で行う。負のときの上限 2147483648 は long に
 * 収まらないためである */
long strtol(const char *s, char **endptr, int base)
{
  char *p;
  int neg;
  int d;
  int any;
  int over;
  unsigned acc;
  unsigned lim;
  unsigned cut;
  unsigned cutd;

  if (endptr) *endptr = s;
  if (base < 0 || base == 1 || base > 36) return 0;
  p = s;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v'
         || *p == '\f' || *p == '\r')
    p++;
  neg = 0;
  if (*p == '+') p++;
  else if (*p == '-') { neg = 1; p++; }
  if ((base == 0 || base == 16) && p[0] == '0'
      && (p[1] == 'x' || p[1] == 'X')
      && digval(p[2]) >= 0 && digval(p[2]) < 16) {
    p = p + 2;
    base = 16;
  } else if (base == 0) {
    if (*p == '0') base = 8;
    else base = 10;
  }
  lim = 0x7fffffff;
  if (neg) lim = lim + 1;               /* 2147483648 (unsigned) */
  cut = lim / (unsigned)base;
  cutd = lim % (unsigned)base;
  acc = 0;
  any = 0;
  over = 0;
  while ((d = digval(*p)) >= 0 && d < base) {
    if (over || acc > cut || (acc == cut && (unsigned)d > cutd)) over = 1;
    else acc = acc * (unsigned)base + (unsigned)d;
    any = 1;
    p++;
  }
  if (!any) return 0;                   /* *endptr は先頭のまま (C89 どおり) */
  if (endptr) *endptr = p;
  if (over) acc = lim;
  if (neg) return (long)(0 - acc);      /* 2147483648 を含めて型変換だけで済む */
  return (long)acc;
}

int atoi(char *s)
{
  return (int)strtol(s, NULL, 10);
}

/* abs(INT_MIN) は未定義 (C89 どおり) */
int abs(int n)
{
  if (n < 0) return -n;
  return n;
}

/* RV32M の div / rem は 0 方向への切捨てで，C89 の要求
 * (quot * denom + rem == numer) と一致する */
div_t div(int numer, int denom)
{
  div_t r;

  r.quot = numer / denom;
  r.rem = numer % denom;
  return r;
}

/* ---- 第 4 部 (libc15) で足した変換 ---- */

unsigned long strtoul(const char *s, char **endptr, int base)
{
    /* 負号を除けば strtol と同じ読み方。あちらは上限で丸めるが，
     * こちらは 32 bit で回る (C の unsigned の規則どおり) */
    unsigned long v;
    int neg;
    int d;

    while (*s == ' ' || *s == '\t') s = s + 1;
    neg = 0;
    if (*s == '+') s = s + 1;
    else if (*s == '-') { neg = 1; s = s + 1; }
    if ((base == 0 || base == 16) && s[0] == '0'
        && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s + 2;
    } else if (base == 0) {
        if (*s == '0') base = 8;
        else base = 10;
    }
    v = 0;
    while (1) {
        d = *s;
        if (d >= '0' && d <= '9') d = d - '0';
        else if (d >= 'a' && d <= 'z') d = d - 'a' + 10;
        else if (d >= 'A' && d <= 'Z') d = d - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        s = s + 1;
    }
    if (endptr != NULL) *endptr = s;
    if (neg) return 0 - v;
    return v;
}

unsigned long long strtoull(const char *s, char **endptr, int base)
{
    unsigned long long v;
    int neg;
    int d;

    while (*s == ' ' || *s == '\t') s = s + 1;
    neg = 0;
    if (*s == '+') s = s + 1;
    else if (*s == '-') { neg = 1; s = s + 1; }
    if ((base == 0 || base == 16) && s[0] == '0'
        && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s + 2;
    } else if (base == 0) {
        if (*s == '0') base = 8;
        else base = 10;
    }
    v = 0;
    while (1) {
        d = *s;
        if (d >= '0' && d <= '9') d = d - '0';
        else if (d >= 'a' && d <= 'z') d = d - 'a' + 10;
        else if (d >= 'A' && d <= 'Z') d = d - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)d;
        s = s + 1;
    }
    if (endptr != NULL) *endptr = s;
    if (neg) return 0ULL - v;
    return v;
}

long long strtoll(const char *s, char **endptr, int base)
{
    return (long long)strtoull(s, endptr, base);
}

long long atoll(char *s)
{
    return strtoll(s, NULL, 10);
}

/* 10 進の浮動小数点。仮数を 64 bit で読み，10 の冪を掛けるか割るか
 * する。冪は 2 分で組むので丸めは数回入る (ホストの strtod の 0.5 ulp
 * より粗い。既知の妥協: docs/stage015-tcc.md 10 章)。
 * 16 進浮動小数点と inf / nan の綴りはまだ読まない */
double strtod(const char *s, char **endptr)
{
    unsigned long long sig;
    int neg;
    int fdig;
    int xv;
    int xneg;
    int e;
    double d;
    double p;

    while (*s == ' ' || *s == '\t') s = s + 1;
    neg = 0;
    if (*s == '+') s = s + 1;
    else if (*s == '-') { neg = 1; s = s + 1; }
    sig = 0;
    fdig = 0;
    while (*s >= '0' && *s <= '9') {
        if (sig < 922337203685477580ULL)
            sig = sig * 10ULL + (unsigned long long)(*s - '0');
        else
            fdig = fdig - 1;    /* 器に入らない桁は指数へ送る (下の e で戻す) */
        s = s + 1;
    }
    if (*s == '.') {
        s = s + 1;
        while (*s >= '0' && *s <= '9') {
            if (sig < 922337203685477580ULL) {
                sig = sig * 10ULL + (unsigned long long)(*s - '0');
                fdig = fdig + 1;
            }
            s = s + 1;
        }
    }
    xv = 0;
    xneg = 0;
    if (*s == 'e' || *s == 'E') {
        s = s + 1;
        if (*s == '+') s = s + 1;
        else if (*s == '-') { xneg = 1; s = s + 1; }
        while (*s >= '0' && *s <= '9') {
            if (xv < 10000) xv = xv * 10 + (*s - '0');
            s = s + 1;
        }
        if (xneg) xv = 0 - xv;
    }
    if (endptr != NULL) *endptr = s;
    /* 整数部が 18 桁を超えたぶんは fdig を負にして送ってある。10 進の
     * 有効数字を 18 桁保つので double (17 桁) には十分である。
     * 上限が 2^63/10 なのは，この後 (long long) へ落とすためである。
     * 2^64/10 にすると sig が 2^63 を超え，符号つきへの変換で負になる。
     * これを忘れると 20 桁の定数が 1/10 になる (2^64 = 1844674407...616 が
     * その例。docs/stage015-tcc.md 12.25) */
    d = (double)(long long)sig;     /* sig < 2^63 なので符号つきで足りる */
    e = xv - fdig;
    p = 10.0;
    if (e < 0) e = 0 - e;
    while (e > 0) {
        if (e & 1) {
            if (xv - fdig < 0) d = d / p;
            else d = d * p;
        }
        p = p * p;
        e = e >> 1;
    }
    if (neg) return 0.0 - d;
    return d;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

/* strtold: long double は double と同じ幅なので strtod をそのまま返す */
long double strtold(const char *s, char **endptr)
{
    return strtod(s, endptr);
}
