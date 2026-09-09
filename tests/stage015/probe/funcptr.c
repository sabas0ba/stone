/* 関数ポインタを，ホストの処理系と突き合わせる (tools/diff17.sh)。
 *
 * **GCC は関数ポインタの表で出来ている。** langhooks / target hooks /
 * gimple の walk はどれも「構造体に関数ポインタを並べ，初期化子で埋め，
 * 添字で呼ぶ」形である。4 に進んだ瞬間に要る。
 *
 * 測るのは，宣言の形が通ることではなく**呼んだ結果**である ——
 * 呼び出し規約や引数の並べ方を間違えると，通ったうえで違う値になる。 */
int putc(int c);

static int pn(long v) {
  char b[24];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + (int)(v % 10)); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
  return 0;
}
static int nl(void) { putc('\n'); return 0; }

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

/* 引数を 8 つ取る (レジスタを溢れる境を跨ぐ) */
static int many(int a, int b, int c, int d, int e, int f, int g, int h) {
  return a * 10000000 + b * 1000000 + c * 100000 + d * 10000
       + e * 1000 + f * 100 + g * 10 + h;
}

static double dbl(double a, double b) { return a * b; }

/* 関数ポインタを取る関数 (qsort の形) */
static int apply(int (*f)(int, int), int a, int b) { return f(a, b); }

/* 関数ポインタを返す関数。**typedef を挟んで書く** —— 直に書く形
 * (`int (*pick(int k))(int, int)`) は cc15ab がまだ受けない。宣言子の
 * 途中で「これは関数定義である」と判る形になり，宣言の読み方そのものを
 * 組み直すことになるので次の世代へ回した (docs/stage018-ext.md 12) */
typedef int (*binop)(int, int);
static binop pick(int k) {
  if (k == 0) return add;
  if (k == 1) return sub;
  return mul;
}

/* 構造体に並べる (hooks の形) */
struct ops {
  char *name;
  int (*fn)(int, int);
  int arity;
};

static struct ops tab[3] = {
  { "add", add, 2 },
  { "sub", sub, 2 },
  { "mul", mul, 2 }
};

/* 配列に並べる */
static int (*fns[3])(int, int) = { add, sub, mul };

int main(void) {
  int i;
  int (*p)(int, int);
  int (*q)(int, int, int, int, int, int, int, int);
  double (*d)(double, double);

  p = add;
  pn(p(3, 4));
  pn((*p)(3, 4));               /* 明に外す形も同じ意味である */
  p = &sub;                     /* & を付けても同じ */
  pn(p(3, 4));
  nl();

  for (i = 0; i < 3; i = i + 1) pn(fns[i](10, 3));
  for (i = 0; i < 3; i = i + 1) pn(tab[i].fn(10, 3));
  for (i = 0; i < 3; i = i + 1) pn(pick(i)(10, 3));
  nl();

  pn(apply(add, 5, 6));
  pn(apply(fns[2], 5, 6));
  pn(apply(tab[1].fn, 5, 6));
  nl();

  q = many;
  pn(q(1, 2, 3, 4, 5, 6, 7, 8));
  d = dbl;
  pn((long)(d(2.5, 4.0) * 100.0));
  nl();

  /* 比べる。同じ関数を指せば等しい */
  p = add;
  pn(p == add);
  pn(p == sub);
  pn(fns[0] == tab[0].fn);
  pn(p != 0);
  nl();

  /* 表を書き換える (hooks を差し替える形) */
  tab[0].fn = mul;
  pn(tab[0].fn(4, 5));
  nl();
  return 0;
}
