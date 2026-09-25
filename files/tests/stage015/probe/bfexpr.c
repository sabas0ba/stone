/* ビットフィールドの幅を整数定数式で書く形を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.11 / stage015/cc15ag.sc。
 *
 * C89 6.5.2.1 の幅は整数定数式である。GCC の real.h は
 * `unsigned int uexp : EXP_BITS;` と書き，EXP_BITS は (32 - 6) に展開される。
 * cc15af までは数のリテラル 1 個だけを受け，構文エラー (1) になっていた。
 *
 * **幅を読み違えれば詰め方が変わり，隣のフィールドを壊す。** 幅いっぱいの
 * 値を書いて読み戻し，隣が無事であることを値で見る。 */
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

#define EXP_BITS (32 - 6)
enum { W = 3 };

struct s {
  unsigned int lo : 2 * 2;       /* 4 */
  unsigned int mid : W + 1;      /* 4 */
  unsigned int : 0;              /* 次の記憶単位へ */
  unsigned int uexp : EXP_BITS;  /* 26 */
  unsigned int sign : 1;
};

int main(void) {
  struct s x;
  x.lo = 15;
  x.mid = 9;
  x.uexp = 67108863;             /* 2^26 - 1: 幅いっぱい */
  x.sign = 1;
  pn((int)x.lo);
  pn((int)x.mid);
  pn((int)x.uexp);
  pn((int)x.sign);
  x.uexp = x.uexp + 1;           /* 幅から溢れて 0 に戻る。sign は無事 */
  pn((int)x.uexp);
  pn((int)x.sign);
  nl();
  return 0;
}
