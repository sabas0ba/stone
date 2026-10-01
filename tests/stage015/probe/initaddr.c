/* 静的初期化子と大域の宣言の形を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.14 / stage015/cc15al.sc。
 *
 * GCC の gcc/ は静的な表に `&global_options.x_flag_foo` や `&tab[3]` を
 * 置き (c-opts / c-format / gtype-desc など)，文字列を括弧で囲み
 * (diagnostic)，extern の宣言に初期化子を付け (c-opts)，関数型の typedef で
 * 関数を宣言する (caller-save)。cc15ak まではいずれも拒んでいた。
 *
 * **変位を取り違えれば別のメンバを指す。** 読み出した値で位置を見る。 */
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

struct inner { int p; int q[4]; };
struct opts { int a; char c; int b; struct inner in; int tail; };

struct opts g = { 1, 'x', 2, { 3, { 4, 5, 6, 7 } }, 8 };
int tab[10] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
struct inner arr[3] = { { 20, { 21, 22, 23, 24 } }, { 30, { 31, 32, 33, 34 } },
                        { 40, { 41, 42, 43, 44 } } };

static int *ptrs[] = {
  &g.a, &g.b, &g.in.p, &g.in.q[2], &g.tail,
  &tab[0], &tab[7], &arr[1].p, &arr[2].q[3], &arr[0].q[0]
};
static char *cp = &g.c;

struct entry { const char *name; int *flag; };
static const struct entry ents[] = {
  { ("alpha"), &g.b },
  { "beta", &tab[9] },
};
static const char *paren = ("gamma");

extern int ext_init = 55;
extern int ext_init;

typedef int binop_t (int, int);
binop_t add2, mul2;
static binop_t sub2;

int add2 (int x, int y) { return x + y; }
int mul2 (int x, int y) { return x * y; }
static int sub2 (int x, int y) { return x - y; }

int main(void) {
  int i;
  binop_t *fp;
  for (i = 0; i < 10; i++) pn(*ptrs[i]);
  pn(*cp);
  nl();
  pn(ents[0].name[0]);
  pn(*ents[0].flag);
  pn(ents[1].name[3]);
  pn(*ents[1].flag);
  pn(paren[4]);
  pn(ext_init);
  fp = mul2;
  pn(add2(3, 4));
  pn(fp(5, 6));
  pn(sub2(9, 20));
  nl();
  return 0;
}
