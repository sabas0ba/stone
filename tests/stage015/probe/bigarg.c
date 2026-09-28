/* 容量と 64 bit の定数式を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.14 / stage015/cc15al.sc。
 *
 * GCC の gcc/ は 1024 バイトを超える構造体を代入し (opts)，引数が 32 個を
 * 超える関数を呼び (insn-emit)，ビットフィールドに HOST_WIDE_INT を足し込み
 * (tree-stdarg)，`1LL << 40` を定数式に書く (i386 / options)。cc15ak までは
 * いずれも拒んでいた。
 *
 * 素の int のビットフィールドは GCC と同じく符号つきで読む (C89 6.5.2.1 では
 * 処理系定義)。cc15ak までは一律に符号なしで，-5 を 65531 と読んでいた。
 *
 * **複写の端と上位語を取り違えれば値が変わる。** 末尾の半端なバイトと，
 * 32 bit を越える定数の上位語を値で見る。 */
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
static void pl(long long v) { pn((int)(v >> 32)); pn((int)v); }
static void nl(void) { putc('\n'); }

struct big { int w[700]; short h; char c; };

static int many (int a0, int a1, int a2, int a3, int a4, int a5, int a6,
                 int a7, int a8, int a9, int a10, int a11, int a12, int a13,
                 int a14, int a15, int a16, int a17, int a18, int a19,
                 int a20, int a21, int a22, int a23, int a24, int a25,
                 int a26, int a27, int a28, int a29, int a30, int a31,
                 int a32, int a33, int a34, int a35, int a36)
{
  return a0 + a1 * 2 + a15 * 3 + a31 * 4 + a32 * 5 + a36 * 6
         + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14
         + a16 + a17 + a18 + a19 + a20 + a21 + a22 + a23 + a24 + a25
         + a26 + a27 + a28 + a29 + a30 + a33 + a34 + a35 + a2;
}

struct bf { unsigned int lo : 5; unsigned int mid : 11; int hi : 16; };

static long long ctab[] = {
  1LL << 40,
  (1LL << 33) + 7,
  0x100000000LL * 3 - 1,
  -(1LL << 35),
  (long long)1 << 50 | 5,
  ~0LL ^ (1LL << 62),
  (0x7fffffffLL + 1) * 2,
  (1LL << 36) >> 4,
  1000000LL * 1000000LL,
  -1LL * 3
};
static int cmp[] = {
  (1LL << 40) > (1LL << 39),
  (1LL << 32) == 0x100000000LL,
  !(1LL << 40),
  (1LL << 40) != 0
};

int main(void) {
  static struct big s;
  struct big d;
  struct bf b;
  long long w;
  int i;
  for (i = 0; i < 700; i++) s.w[i] = i * 3 + 1;
  s.h = 1234;
  s.c = 'z';
  d = s;
  pn(d.w[0]); pn(d.w[255]); pn(d.w[256]); pn(d.w[699]); pn(d.h); pn(d.c);
  pn((int)sizeof(struct big));
  nl();
  pn(many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
          20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
          37));
  nl();
  b.lo = 3;
  b.mid = 100;
  b.hi = -5;
  w = (1LL << 40) + 9;
  b.lo += w;
  b.mid -= w;
  b.hi *= w;
  pn(b.lo); pn(b.mid); pn(b.hi);
  b.hi >>= 2;
  pn(b.hi);
  b.hi /= 3;
  pn(b.hi);
  pn(++b.hi);
  pn(b.hi--);
  pn(b.hi);
  nl();
  for (i = 0; i < 10; i++) pl(ctab[i]);
  nl();
  for (i = 0; i < 4; i++) pn(cmp[i]);
  nl();
  return 0;
}
