/* 構造体を 1100 個宣言した後の型の取り違えを，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.12 / stage015/cc15ah.sc。
 *
 * cc15ag までは型の基底番号の範囲が重なっていた。構造体の番号は 2 から
 * 登録順に振るので，298 個を超えると void / unsigned int などと，1022 個を
 * 超えると配列型と同じ番号になり，構造体と判定されて 5 で止まる。
 * GCC の gcc/ の単位は構造体を 300 個以上宣言する。
 *
 * 構造体を先に並べ，その後で unsigned int のメンバ・ビットフィールド・
 * 返却値・関数ポインタの返却値・配列を使う。 */
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

/* 10 個 -> 100 個 -> 1000 個と組む。名前は s_<3 桁> になる */
#define S1(n) struct s_##n { int a; };
#define S10(n) S1(n##0) S1(n##1) S1(n##2) S1(n##3) S1(n##4) \
               S1(n##5) S1(n##6) S1(n##7) S1(n##8) S1(n##9)
#define S100(n) S10(n##0) S10(n##1) S10(n##2) S10(n##3) S10(n##4) \
                S10(n##5) S10(n##6) S10(n##7) S10(n##8) S10(n##9)
S100(a) S100(b) S100(c) S100(d) S100(e)
S100(f) S100(g) S100(h) S100(i) S100(j)
S100(k)

struct vec_prefix { unsigned int num; unsigned int alloc; };
struct vec { struct vec_prefix prefix; int v[4]; };
struct bits { unsigned int lo : 4; unsigned int hi : 12; };

static unsigned int twice(unsigned int x) { return x + x; }

int main(void) {
  struct vec x;
  struct vec *p;
  struct bits b;
  unsigned int (*fp)(unsigned int);
  int arr[3];
  p = &x;
  ((p) ? &(*p) : 0)->prefix.num = 5;   /* GCC の vec.h の VEC_BASE の形 */
  x.prefix.alloc = 7;
  b.lo = 9;
  b.hi = 300;
  fp = twice;
  arr[0] = 1; arr[1] = 2; arr[2] = 3;
  pn((int)x.prefix.num);
  pn((int)x.prefix.alloc);
  pn((int)b.lo);
  pn((int)b.hi);
  pn((int)twice(21));
  pn((int)fp(50));
  pn(arr[0] + arr[1] + arr[2]);
  pn((int)sizeof(struct s_k99));
  nl();
  return 0;
}
