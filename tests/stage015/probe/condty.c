/* ?: の結果の型を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.15。
 *
 * 2 つの腕が算術型なら，結果の型は通常の算術変換の型である (C89 6.3.15)。
 * `c ? 0 : u` (u は unsigned) は unsigned で，後に続く右シフト・割り算・
 * 大小比較は符号なしになる。GCC の real.c の lshift_significand が
 * `(ofs + i >= SIGSZ ? 0 : a->sig[k]) >> (32 - n)` と書き，腕の片方の型で
 * 算術シフトすると上位が 1 で埋まって，浮動小数点の定数の仮数が壊れた。 */
int putc(int c);

static void hx(unsigned v) {
  int i;
  int d;
  for (i = 7; i >= 0; i--) {
    d = (v >> (i * 4)) & 15;
    putc(d < 10 ? '0' + d : 'a' + d - 10);
  }
  putc(' ');
}

static unsigned big = 0x80000000U;
static unsigned short us = 65535;
static int k = 1;

int main(void) {
  hx((k ? big : 0) >> 28);
  hx((k ? 0 : big) >> 28);
  hx((!k ? 0 : big) >> 28);
  hx((!k ? big : 0) >> 28);
  hx((unsigned) ((!k ? 0 : big) / 3));
  hx(((!k ? 0 : big) > 5) ? 1 : 0);
  hx(((!k ? -1 : big) > 5) ? 1 : 0);
  hx((unsigned) ((k ? us : -1) >> 4));     /* unsigned short は int に格上げ */
  hx((unsigned) ((k ? 1 : 2.5) * 2));      /* int と double は double */
  putc('\n');
  return 0;
}
