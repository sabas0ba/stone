/* 整数の変換・格上げ・けた移動を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。
 *
 * **この類は「動くが違う」しか起きない。** 通らないのではなく，値が
 * 静かに変わる。cc15u (複合代入が符号を見ていなかった) と同じ場所で，
 * あのときは我々のソースが `%=` を使っていないので永久に表に出なかった。
 * 外を的にする以外に見つける道が無い。 */
int putc(int c);

/* **最小値を符号反転しない。** long は我々が 32 bit，ホストが 64 bit
 * なので，-v で書くと -2147483648 の扱いが分かれる (器の誤りではない) */
static int pn(long v) {
  char b[24];
  unsigned long u;
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; u = (unsigned long)(-(v + 1)) + 1UL; }
  else u = (unsigned long)v;
  if (u == 0) { b[n] = '0'; n = 1; }
  while (u > 0) { b[n] = (char)('0' + (int)(u % 10)); u = u / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
  return 0;
}
static int pu(unsigned long v) {
  char b[24];
  int n;
  n = 0;
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + (int)(v % 10)); v = v / 10; n = n + 1; }
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
  return 0;
}
static int nl(void) { putc('\n'); return 0; }

static signed char sc;
static unsigned char uc;
static short sh;
static unsigned short uh;
static int si;
static unsigned int ui;

int main(void) {
  int i;

  /* 1. 切り詰め。範囲を超える代入は処理系定義だが，2 の補数で切るのが
   * 実際のすべてである */
  sc = 200;  pn(sc);
  uc = 200;  pn(uc);
  sc = -1;   pn(sc);
  uc = -1;   pn(uc);
  sh = 40000; pn(sh);
  uh = 40000; pn(uh);
  sh = -40000; pn(sh);
  nl();

  /* 2. 既定の格上げ。char / short は int になる */
  sc = -1;
  uc = 255;
  pn(sc + 1);
  pn(uc + 1);
  pn(sc * 2);
  pn(uc * 2);
  pn(sc < 0);
  pn(uc < 0);
  nl();

  /* 3. 符号つきと符号なしの比べ方。**int と unsigned int を比べると
   * int が unsigned になる** (C89 6.2.1.1)。-1 < 1u は偽である */
  si = -1;
  ui = 1;
  pn(si < (int)ui);
  pn(si < ui);
  pn((unsigned)si > ui);
  pn(-1 < 1u);
  nl();

  /* 4. けた移動。負の値の右移動は処理系定義 (算術移動が実際のすべて)。
   * 移動の幅は左辺の格上げ後の型で決まる */
  si = -8;
  pn(si >> 1);
  pn(si >> 2);
  pn(si << 1);
  ui = 0x80000000u;
  pu(ui >> 1);
  pu(ui >> 31);
  pu(ui << 1);
  sc = -8;
  pn(sc >> 1);
  uc = 0x80;
  pn(uc >> 1);
  pn(uc << 1);
  nl();

  /* 5. 割り算と剰余。C89 は 0 へ切り捨てる (処理系定義だったのは C89
   * だけで，実際のすべては 0 方向である)。剰余の符号は被除数に従う */
  pn(7 / 2);
  pn(-7 / 2);
  pn(7 / -2);
  pn(-7 / -2);
  pn(7 % 2);
  pn(-7 % 2);
  pn(7 % -2);
  pn(-7 % -2);
  nl();

  /* 6. 複合代入。**右辺の型ではなく左辺の型で行う** (cc15u が踏んだ所) */
  ui = 10;
  ui %= 3;   pu(ui);
  ui = 10;
  ui /= 3;   pu(ui);
  ui = 0xF0000000u;
  ui >>= 4;  pu(ui);
  si = -16;
  si >>= 2;  pn(si);
  si = 5;
  si <<= 3;  pn(si);
  sc = 100;
  sc += 100; pn(sc);
  uc = 200;
  uc += 100; pn(uc);
  uh = 60000;
  uh += 10000; pn(uh);
  nl();

  /* 7. ビット演算。格上げしてから行う */
  sc = -1;
  pn(sc & 0xff);
  pn(sc | 0);
  pn(sc ^ 0);
  pn(~sc);
  uc = 0xf0;
  pn(uc & 0x0f);
  pn(~uc);
  pu(~0u);
  nl();

  /* 8. 前置と後置。値と副作用の順 */
  si = 5;
  pn(si++);
  pn(si);
  pn(++si);
  pn(si--);
  pn(--si);
  nl();

  /* 9. 条件演算子の型。両枝の共通型へ寄せる */
  si = -1;
  ui = 1;
  pu(1 ? (unsigned)si : ui);
  pn(0 ? 1 : -1);
  sc = 1;
  pn(sc ? sc : 0);
  nl();

  /* 10. 文字の符号。**char が符号つきかは処理系定義**である。
   * 我々もホストも RISC-V / x86 の既定に従うはずだが，違えばここで出る */
  {
    char c;
    c = -1;
    pn(c < 0);
    c = 200;
    pn(c < 0);
  }
  nl();

  /* 11. 定数の型。0xFFFFFFFF は unsigned int である (C89 6.1.3.2) */
  pu(0xFFFFFFFF);
  pn(0xFFFFFFFF > 0);
  pu(2147483648u);
  pn(-2147483647 - 1);
  nl();

  /* 12. 添字とポインタの引き算 */
  {
    int a[8];
    int *p;
    int *q;
    for (i = 0; i < 8; i = i + 1) a[i] = i * i;
    p = a;
    q = a + 5;
    pn((long)(q - p));
    pn(*(p + 3));
    pn(p[3]);
    pn(3[p]);
    pn(*(q - 2));
    nl();
  }
  return 0;
}
