/* 64 bit の値を 32 bit の文脈で使う形を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.13 / stage015/cc15ak.sc。
 *
 * GCC の gcc/ は HOST_WIDE_INT (long long) を添字・複合代入の右辺・switch の
 * 制御式・?: の片方に使う。cc15aj までは 5 で拒んでいた。
 *
 * **切り詰めてよい所と，上位語が効く所がある。** / と % の右辺，
 * switch の 32 bit に収まらない値，?: の then 側の符号拡張は，上位語を
 * 捨てると値が変わる。それぞれ境目の値を並べる。 */
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

static int sw(long long x) {
  switch (x) {
  case 0: return 10;
  case -1: return 20;
  case 7: return 30;
  default: return 40;
  }
}

static int swnd(long long x) {
  int r;
  r = 1;
  switch (x) {
  case 0: r = 2; break;
  case 5: r = 3; break;
  }
  return r;
}

static long long pick(int c, int a, long long b) { return c ? a : b; }
static unsigned long long upick(int c, unsigned int a, unsigned long long b) {
  return c ? a : b;
}

int main(void) {
  int tab[5];
  long long i;
  int x;
  unsigned int u;
  char ch;
  long long big;
  double d;
  tab[0] = 3; tab[1] = 1; tab[2] = 4; tab[3] = 1; tab[4] = 5;
  i = 4;
  pn(tab[i]);                            /* 添字 */
  i = 2;
  pn(tab[i - 1] + tab[i]);
  x = 1000;
  big = 3;
  x /= big;                              /* 333 */
  pn(x);
  x = -7;
  big = 2;
  x %= big;                              /* -1 */
  pn(x);
  x = 100;
  big = 4294967296LL + 2;                /* 上位語が効く: 100 / (2^32 + 2) = 0 */
  x /= big;
  pn(x);
  x = 5;
  big = 4294967296LL + 3;                /* 下位語だけで決まる: 5 + 3 = 8 */
  x += big;
  pn(x);
  u = 1;
  u |= (long long)1 << 4;
  pn((int)u);
  ch = 100;
  ch += (long long)300;                  /* char へ戻す: 400 & 255 = 144 */
  pn(ch & 255);
  pn(sw(0));
  pn(sw(-1));
  pn(sw(7));
  pn(sw(4294967296LL));                  /* 下位語は 0 だが case 0 ではない */
  pn(sw(4294967296LL + 7));
  pn(swnd(4294967296LL + 5));
  pn(swnd(5));
  big = pick(1, -5, 99);                 /* then の符号で広げる */
  pn((int)(big >> 32));
  pn((int)big);
  big = pick(0, -5, 4294967296LL * 3);
  pn((int)(big >> 32));
  pn((int)((unsigned long long)upick(1, 4294967295U, 1) >> 32));
  d = 1 ? 3 : 2.5;                       /* int -> double */
  pn((int)(d * 10));
  d = 0 ? 3 : 2.5;
  pn((int)(d * 10));
  nl();
  return 0;
}
