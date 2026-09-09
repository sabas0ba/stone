/* 構造体・共用体・入れ子の初期化子を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。
 *
 * GCC の C は「構造体を値で渡し，値で返し，入れ子の初期化子で表を作る」
 * 形を至る所で使う。**通ったうえで違う値になる**のがこの類の怖さで，
 * 引数の並べ方や詰め物の扱いを間違えると，添字がずれた表を静かに読む。 */
int putc(int c);

static int pn(long v) {
  char b[24];
  unsigned long u;
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; u = (unsigned long)(-(v + 1)) + 1UL; }
  else u = (unsigned long)v;
  if (u == 0) { b[n] = '0'; n = 1; }
  while (u > 0) { b[n] = (char)('0' + (int)(u % 10)); u = u / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
  return 0;
}
static int nl(void) { putc('\n'); return 0; }

struct pt { int x; int y; };
struct box { struct pt lo; struct pt hi; char tag; };
union u { int i; char c[4]; short h[2]; };

struct row { char *name; int v[3]; struct pt p; };

static struct row tab[3] = {
  { "a", { 1, 2, 3 }, { 10, 11 } },
  { "b", { 4, 5 } },                    /* 足りない分は 0 */
  { "c" }
};

static struct pt grid[2][3] = {
  { { 1, 1 }, { 1, 2 }, { 1, 3 } },
  { { 2, 1 }, { 2, 2 } }
};

static int flat[6] = { 1, 2, 3 };
static char msg[8] = "hi";

/* 値で渡し，値で返す */
static struct pt mkpt(int x, int y) {
  struct pt p;
  p.x = x;
  p.y = y;
  return p;
}
static int ptsum(struct pt p) { return p.x + p.y; }
static struct box grow(struct box b, int d) {
  b.lo.x = b.lo.x - d;
  b.lo.y = b.lo.y - d;
  b.hi.x = b.hi.x + d;
  b.hi.y = b.hi.y + d;
  return b;
}
/* 引数の位置がずれるか (整数と構造体が混ざる形) */
static int mix(int a, struct pt p, int b, struct pt q, int c) {
  return a * 100000 + p.x * 10000 + p.y * 1000 + b * 100 + q.x * 10 + q.y + c;
}

int main(void) {
  struct pt p;
  struct box b;
  union u u;
  int i;
  int j;

  pn((long)sizeof(struct pt));
  pn((long)sizeof(struct box));
  pn((long)sizeof(union u));
  /* **ポインタを含む構造体の大きさは測らない** —— 我々は ILP32，
   * ホストは LP64 なので，器の誤りでない差が出る (struct row は
   * 24 と 32)。詰め方そのものは struct box と grid で見ている */
  pn((long)sizeof(grid));
  nl();

  p = mkpt(3, 4);
  pn(p.x); pn(p.y); pn(ptsum(p));
  pn(ptsum(mkpt(5, 6)));
  nl();

  b.lo = mkpt(1, 2);
  b.hi = mkpt(9, 8);
  b.tag = 'z';
  b = grow(b, 2);
  pn(b.lo.x); pn(b.lo.y); pn(b.hi.x); pn(b.hi.y); pn(b.tag);
  nl();

  pn(mix(1, mkpt(2, 3), 4, mkpt(5, 6), 7));
  nl();

  /* 共用体。**同じ場所を別の型で見る** */
  u.i = 0x01020304;
  pn(u.c[0] & 255); pn(u.c[1] & 255); pn(u.c[2] & 255); pn(u.c[3] & 255);
  pn(u.h[0] & 0xffff); pn(u.h[1] & 0xffff);
  u.c[0] = 0;
  pn(u.i & 0xffff);
  nl();

  /* 入れ子の初期化子。足りない分は 0 */
  for (i = 0; i < 3; i = i + 1) {
    pn(tab[i].name[0]);
    pn(tab[i].v[0]); pn(tab[i].v[1]); pn(tab[i].v[2]);
    pn(tab[i].p.x); pn(tab[i].p.y);
  }
  nl();
  for (i = 0; i < 2; i = i + 1)
    for (j = 0; j < 3; j = j + 1) { pn(grid[i][j].x); pn(grid[i][j].y); }
  nl();
  for (i = 0; i < 6; i = i + 1) pn(flat[i]);
  for (i = 0; i < 8; i = i + 1) pn(msg[i]);
  nl();

  /* 写しと比べ */
  {
    struct box c;
    c = b;
    c.lo.x = 100;
    pn(b.lo.x); pn(c.lo.x); pn(c.hi.y);
  }
  nl();

  /* 構造体の配列を歩く (詰め物が違えば添字がずれる) */
  {
    struct box arr[3];
    for (i = 0; i < 3; i = i + 1) {
      arr[i].lo = mkpt(i, i + 1);
      arr[i].hi = mkpt(i + 2, i + 3);
      arr[i].tag = (char)('a' + i);
    }
    for (i = 0; i < 3; i = i + 1) {
      pn(arr[i].lo.x); pn(arr[i].hi.y); pn(arr[i].tag);
    }
    pn((long)((char *)&arr[1] - (char *)&arr[0]));
  }
  nl();

  /* 構造体へのポインタと -> の連鎖 */
  {
    struct box *q;
    q = &b;
    pn(q->lo.x); pn(q->hi.y); pn((&q->lo)->y);
  }
  nl();
  return 0;
}
