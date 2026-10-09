/* 構造体を返す関数を関数へのポインタで呼ぶ形を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.12 / stage015/cc15ai.sc。
 *
 * GCC の gcc/ は targetm などのフックの表を関数へのポインタで呼び，
 * double_int (2 語の構造体) などを値で返す。cc15ah までは返却値を
 * 引き取る語数を出力段で決められないとして 5 で拒んでいた。
 *
 * **語数を取り違えればデータスタックがずれる。** 1 語・2 語・3 語の構造体を
 * 返させ，呼出しの後に積んだ引数と局所変数が無事であることを値で見る。 */
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

struct one { int a; };
struct two { int lo; int hi; };                 /* double_int と同じ形 */
struct three { int x; int y; int z; };

static struct one mk1(int a) { struct one r; r.a = a; return r; }
static struct two mk2(int lo, int hi) {
  struct two r;
  r.lo = lo;
  r.hi = hi;
  return r;
}
static struct three mk3(int x) {
  struct three r;
  r.x = x;
  r.y = x * 2;
  r.z = x * 3;
  return r;
}

struct hooks {
  struct two (*pair)(int, int);
  struct three (*triple)(int);
};

static int sum3(struct three t) { return t.x + t.y + t.z; }

int main(void) {
  struct one (*f1)(int);
  struct two (*f2)(int, int);
  struct hooks h;
  struct hooks *hp;
  struct one o;
  struct two t;
  struct three u;
  int guard;
  guard = 77;
  f1 = mk1;
  f2 = mk2;
  h.pair = mk2;
  h.triple = mk3;
  hp = &h;
  o = f1(5);
  t = f2(6, 7);
  u = hp->triple(4);
  pn(o.a);
  pn(t.lo);
  pn(t.hi);
  pn(u.x);
  pn(u.y);
  pn(u.z);
  pn(hp->pair(8, 9).hi);                         /* 返却値のメンバを直に読む */
  pn(sum3(h.triple(10)));                        /* 返却値をそのまま値渡しする */
  pn((*f2)(1, 2).lo + f1(3).a);
  pn(guard);
  nl();
  return 0;
}
