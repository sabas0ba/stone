/* 関数の中の宣言の形を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.14 / stage015/cc15al.sc。
 *
 * GCC の gcc/ は関数型の仮引数 (sel-sched)，配列へのポインタ (reload1)，
 * 関数の中の enum (c-pragma / tree-vect-stmts)，大きさを省いた局所配列
 * (toplev)，入れ子の局所の構造体の初期化子 (tree-ssa-ccp) を書く。cc15ak までは
 * いずれも拒んでいた。
 *
 * **列挙定数の有効範囲を取り違えれば外側の値を読む。** ブロックの内外で同じ
 * 名前の値を並べる。 */
int putc(int c);

static void pn(int v) {
  char b[16];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void nl(void) { putc('\n'); }

enum { RED = 1, GREEN = 2 };

static int twice (int x) { return x * 2; }
static int apply (int f (int), int v) { return f (v) + 1; }
static int apply2 (int f (int), int v) { return (*f) (v) + 2; }

static int rows[3][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };
static int sumrow (int (*r)[4], int i) { return r[i][0] + r[i][3]; }
typedef int row4[4];
static int sumrow2 (row4 (*r), int i) { return (*r)[i] * 10; }   /* caller-save.c の形 */

struct pt { int x; int y; };
struct box { int tag; struct pt lo; struct pt hi; int k[2]; };

static int enums (void) {
  int s;
  s = RED * 10;
  {
    enum { RED = 7, BLUE };
    s = s + RED * 100 + BLUE;
  }
  s = s + RED;
  return s;
}

int main(void) {
  int (*p)[4];
  int a[] = { 3, 1, 4, 1, 5 };
  char str[] = "stone";
  struct box bx = { 1, { 2, 3 }, { 4, 5 }, { 6, 7 } };
  struct box by = { 8, { 9 } };
  enum { GREEN = 30 };
  pn(apply(twice, 5));
  pn(apply2(twice, 6));
  p = rows;
  pn(p[1][2]);
  p = p + 2;
  pn((*p)[3]);
  pn(sumrow(rows, 1));
  pn(sumrow2(&rows[2], 3));
  nl();
  pn(enums());
  pn(GREEN);
  pn((int)sizeof(a));
  pn(a[4]);
  pn((int)sizeof(str));
  pn(str[4]);
  nl();
  pn(bx.tag); pn(bx.lo.x); pn(bx.lo.y); pn(bx.hi.x); pn(bx.hi.y);
  pn(bx.k[0]); pn(bx.k[1]);
  pn(by.tag); pn(by.lo.x); pn(by.lo.y); pn(by.hi.y); pn(by.k[1]);
  nl();
  return 0;
}
