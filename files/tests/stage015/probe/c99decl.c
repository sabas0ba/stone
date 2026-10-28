/* C99 の宣言の置き場を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.15 / stage015/cc15ao.sc。
 *
 * GMP 6.3 は文の後の宣言 (mpn/get_str.c など) と for の初期化節の宣言
 * (primesieve.c など) を使う。cc15an までは前者を宣言の始まりと読めず，
 * 後者を式として読んで止まっていた。
 *
 * `const static` (修飾子の後の記憶域クラス。C89 6.5) も置く。
 *
 * **有効範囲を取り違えると外側の同じ名前を壊す。** for の変数と外側の
 * 同名の変数，複文の途中で宣言した変数の初期化の順を値で見る。 */
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

const static int cs = 7;        /* 修飾子の後の記憶域クラス (GMP の mpq/cmp.c) */

int main(void) {
  const static int ls = 9;
  int i = 100;
  int s = 0;
  for (int i = 0; i < 5; i++) s = s + i;
  pn(s);
  pn(i);                        /* 外側の i は 100 のまま */
  s = s * 2;
  int t = s + 1;                /* 文の後の宣言。初期化は s を変えた後 */
  pn(t);
  for (int j = 0, k = 10; j < 3; j++, k--) {
    int m = j * k;
    s = s + m;
  }
  pn(s);
  {
    s = s + 1;
    int u = s * 3;
    pn(u);
    for (unsigned w = 3; w > 0; w--) u = u - (int)w;
    pn(u);
  }
  pn(cs + ls);
  nl();
  return 0;
}
