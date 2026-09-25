/* 空の括弧の関数宣言を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.10 / stage015/cc15af.sc。
 *
 * C89 6.5.4.3 —— 定義でない関数宣言子の識別子並びが空なら，仮引数の
 * 個数も型も与えない。`int f();` はプロトタイプではない。先にプロトタイプ
 * があれば，合成型 (6.1.2.6) はプロトタイプのままである。
 *
 * cc15ae までは `int f();` を仮引数 0 個のプロトタイプとして控え，実引数の
 * ある呼出しを個数の不一致 (5) で拒んでいた。GCC 4.7.4 の libiberty/regex
 * が `<stdlib.h>` の後で `char *realloc ();` と宣言し直す形で止まっていた。
 *
 * **通るだけでは足りない。** 宣言が判らない呼出しは実引数をそのまま積む
 * ので，積み方を誤れば値が変わる。どの形も値を出して突き合わせる。 */
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

/* 1. プロトタイプの後の空の括弧の再宣言。合成型はプロトタイプのまま */
int add2(int a, int b);
int add2();

/* 2. 空の括弧の宣言だけがあり，定義は後ろの K&R 形式 */
static int dig3();

/* 3. 空の括弧の宣言と，空の括弧の定義 (仮引数 0 個) */
static int five();

int main(void) {
  pn(add2(3, 4));
  pn(dig3(1, 2, 3));
  pn(five());
  nl();
  return 0;
}

int add2(int a, int b) { return a + b; }

static int dig3(a, b, c) int a; int b; int c; { return a * 100 + b * 10 + c; }

static int five() { return 5; }
