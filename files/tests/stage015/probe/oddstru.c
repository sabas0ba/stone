/* 大きさが 4 の倍数でない構造体の値渡しと返却を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.12 / stage015/cc15ai.sc。
 *
 * 構造体の値はデータスタックに語単位で積む。大きさ 1 や 5 の構造体で
 * 語数を切り捨てたり，引取り先を大きさそのままで取ったりすると，隣の
 * 局所変数を壊し，データスタックの位置がずれる。名前つきの呼出しと
 * 関数へのポインタを通した呼出しの両方で，前後の値が無事であることを見る。 */
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

struct tiny { char c; };
struct five { char s[5]; };
struct word { int v; };

static struct tiny mkt(int c) { struct tiny r; r.c = (char)c; return r; }
static struct five mkf(int c) {
  struct five r;
  int i;
  for (i = 0; i < 5; i++) r.s[i] = (char)(c + i);
  return r;
}
static int sumf(struct five f, int k) {
  return f.s[0] + f.s[4] + k;
}
static int sumt(struct tiny a, struct tiny b, int k) {
  return a.c * 100 + b.c * 10 + k;
}
static int getwd1(struct word w, int k) { return w.v + k; }

int main(void) {
  char before;
  struct tiny t;
  char mid;
  struct five f;
  char after;
  struct tiny (*pt)(int);
  struct five (*pf)(int);
  struct word w;
  struct tiny t2;
  int i;
  int acc;
  before = 11;
  mid = 22;
  after = 33;
  pt = mkt;
  pf = mkf;
  t = mkt(7);
  pn(t.c);
  f = mkf(40);
  pn(f.s[0] + f.s[4]);
  t = pt(9);
  pn(t.c);
  f = pf(50);
  pn(f.s[1] + f.s[3]);
  pn(sumf(f, 3));
  w.v = 1000;
  pn(getwd1(w, 5));
  t2.c = 4;
  pn(sumt(t, t2, 6));
  acc = 0;
  for (i = 0; i < 20; i++) acc = acc + pt(i).c + pf(i).s[4];
  pn(acc);
  pn(before);
  pn(mid);
  pn(after);
  nl();
  return 0;
}
