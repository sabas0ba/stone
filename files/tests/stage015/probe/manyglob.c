/* 大域記号を 8300 個宣言した単位を訳せるか (docs/stage017-gcc.md 8.12 /
 * stage015/cc15ai.sc)。
 *
 * cc15ah までは大域記号表が 8192 個で，GCC の gcc/ の単位は extern の宣言と
 * VEC マクロが展開する static 関数でこれを使い切り，6 で止まっていた。
 * cc15ai は 32768 個へ広げ，探索をハッシュ表にした。
 *
 * **bare のリンカ (stage008 の ld) は 1 オブジェクト 8192 記号までなので，
 * リンクして走らせることはできない。** tools/diff17.sh は理由を出して省略し，
 * tests/stage015 が翻訳の通ることを見る。main は離れた位置の記号と似た名前の
 * 記号を読み書きする形にしてあり，リンカが追いついたときにそのまま値を
 * 突き合わせられる。 */
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

/* 10 個 -> 100 個 -> 1000 個と組む。名前は g_<1 文字><3 桁> になる */
#define G1(n) static int g_##n;
#define G10(n) G1(n##0) G1(n##1) G1(n##2) G1(n##3) G1(n##4) \
               G1(n##5) G1(n##6) G1(n##7) G1(n##8) G1(n##9)
#define G100(n) G10(n##0) G10(n##1) G10(n##2) G10(n##3) G10(n##4) \
                G10(n##5) G10(n##6) G10(n##7) G10(n##8) G10(n##9)
#define G1000(n) G100(n##0) G100(n##1) G100(n##2) G100(n##3) G100(n##4) \
                 G100(n##5) G100(n##6) G100(n##7) G100(n##8) G100(n##9)
G1000(a) G1000(b) G1000(c) G1000(d) G1000(e)
G1000(f) G1000(g) G1000(h)
G100(i0) G100(i1) G100(i2)

static int last(int k) { return k + 1; }

int main(void) {
  g_a000 = 1;
  g_e499 = 2;
  g_i299 = 3;
  g_i298 = 4;
  g_h999 = 5;
  pn(g_a000);
  pn(g_e499);
  pn(g_i299);
  pn(g_i298);
  pn(g_h999);
  pn(g_b001);
  pn(last(g_i299 + g_i298));
  nl();
  return 0;
}
