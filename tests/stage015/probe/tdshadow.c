/* typedef 名の遮蔽と括弧で囲んだ typedef 名を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.13 / stage015/cc15aj.sc。
 *
 * C89 6.1.2.1 では内側で宣言した名前が外側の同じ名前を隠し，typedef 名も
 * 例外ではない。GCC は typedef 名 partition / edge と同じ名前の仮引数や
 * 局所変数を使い，`(partition >= 0 && ...)` や `edge = ...;` と書く。
 * 型名と読めばキャストや宣言になり，式と読めば値になる。**読み違えれば
 * 構文エラーか，別の値になる。**
 *
 * 括弧で囲んだ typedef 名 `typedef T (name) (params);` (GCC の
 * lto-streamer.h) も並べる。 */
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

typedef struct partition_def { int n; } *partition;
typedef struct edge_def { int w; } *edge;
typedef int count;
typedef int (add_f) (int, int);
typedef double real;

static int add2(int a, int b) { return a + b; }

/* 仮引数 partition が typedef 名を隠す */
static int check(int partition) {
  return (partition >= 0 && partition <= 10) ? partition * 2 : -1;
}

/* 局所変数 edge が typedef 名を隠し，式文の先頭に来る */
static int walk(struct edge_def *list, int k) {
  struct edge_def *edge;
  int s;
  s = 0;
  edge = list;
  while (k > 0) {
    s = s + edge->w;
    edge = edge + 1;
    k = k - 1;
  }
  return s;
}

/* 遮蔽は内側の複文を抜けると終わる */
static int scope(void) {
  int r;
  r = 0;
  {
    int count;
    count = 7;
    r = r + count;
  }
  {
    count c;
    c = 5;
    r = r + c;
  }
  return r;
}

/* 名前の有効範囲は宣言子を読み終えた直後から始まる (C89 6.1.2.1)。
 * 配列の大きさの sizeof (real) はまだ typedef 名 real (double) を指す */
static int declscope(void) {
  int real[sizeof(real)];
  real[7] = 3;
  return (int)sizeof(real) + real[7];
}

/* 内側の typedef が外側の同じ名前の typedef を隠す */
static int inner(void) {
  typedef char count;
  count c;
  c = (count)300;              /* char に切り詰められる */
  return (int)sizeof(count) * 1000 + (c & 255);
}

int main(void) {
  struct edge_def es[3];
  struct partition_def pd;
  partition pp;
  add_f *fp;
  es[0].w = 1; es[1].w = 20; es[2].w = 300;
  pd.n = 9;
  pp = &pd;
  fp = add2;
  pn(check(4));
  pn(check(11));
  pn(walk(es, 3));
  pn(scope());
  pn(inner());
  pn(pp->n);
  pn(fp(40, 2));
  pn((int)sizeof(count));
  pn(declscope());
  nl();
  return 0;
}
