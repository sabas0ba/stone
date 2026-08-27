/* **型修飾子は宣言指定子の列のどこに来てもよい** (C89 6.5)。
 *
 *     declaration-specifiers:
 *         storage-class-specifier declaration-specifiers(opt)
 *         type-specifier         declaration-specifiers(opt)
 *         type-qualifier         declaration-specifiers(opt)
 *
 * 順序の制約は無い。`unsigned const char` も `const unsigned char` も
 * `unsigned char const` も同じ型である。
 *
 * cc15s までは ptype() が const / volatile を**列の先頭と末尾でしか**
 * 読んでいなかった。整数型指定子を集める環に修飾子の枝が無いので，
 * `unsigned const char` は `const` で環を抜けて `unsigned` (= t_uint)
 * を返し，残った `char` を宣言子として読もうとして拒んでいた。
 * cc15t で直した (docs/stage017-gcc.md 5.1)。
 *
 * 表に出た形は zlib である。gzread.c が
 *
 *     local int gz_load(gz_statep state, unsigned char *buf, ...)
 *     ...
 *     unsigned const char *scan;      / * gzread.c 内 * /
 *
 * を書いており，22 単位のうちこの 1 つだけが訳せなかった。
 *
 * **拒むので bad ではなく gap だった** —— 値が壊れる類ではない。
 * それでも「C89 が許す形を拒む」は台帳の穴である。 */
int putc(int c);

/* 大域でも同じ列を通る */
unsigned const char g_uc = 200;
static const unsigned int g_cui = 4000000000u;
long const int g_cli = -5;
unsigned long const g_ulc = 7;

struct s { unsigned const char c; int const i; };
static struct s g_s = { 9, 11 };

/* 引数の位置 (zlib が書いていた形そのもの) */
int sum(unsigned const char *s, int n) {
  int t;
  t = 0;
  while (n > 0) { t = t + *s; s = s + 1; n = n - 1; }
  return t;
}

/* 戻り値の位置。修飾子が型の前・間・後ろのいずれでも同じ型である */
unsigned const char retq(void) { return 250; }
const unsigned char retq2(void) { return 250; }
unsigned char const retq3(void) { return 250; }

int f(void) {
  unsigned const char a[3];
  volatile unsigned int vu;
  unsigned volatile int uv;
  short const int sc;
  const short cs;
  unsigned const long ul;

  /* 大域 */
  if (g_uc != 200) return 'n';
  if (g_cui != 4000000000u) return 'n';
  if (g_cli != -5) return 'n';
  if (g_ulc != 7) return 'n';
  if (g_s.c != 9 || g_s.i != 11) return 'n';

  /* **符号は修飾子の位置で変わらない。** unsigned が効いていないと
   * 200 が -56 になるので，ここで捕まる */
  if (retq() != 250) return 'n';
  if (retq2() != 250) return 'n';
  if (retq3() != 250) return 'n';

  /* 局所。const と書いてあっても本処理系は検査に使わないので書ける */
  a[0] = 1; a[1] = 2; a[2] = 250;
  if (sum(a, 3) != 253) return 'n';

  vu = 3000000000u; if (vu / 2 != 1500000000u) return 'n';
  uv = 3000000000u; if (uv / 2 != 1500000000u) return 'n';
  sc = -3; if (sc != -3) return 'n';
  cs = -4; if (cs != -4) return 'n';
  ul = 4000000000u; if (ul / 2 != 2000000000u) return 'n';

  /* 幅も変わらない */
  if (sizeof(unsigned const char) != 1) return 'n';
  if (sizeof(const unsigned char) != 1) return 'n';
  if (sizeof(unsigned const short) != 2) return 'n';
  if (sizeof(unsigned const int) != 4) return 'n';
  if (sizeof(long const long) != 8) return 'n';

  /* 型変換の中でも同じ */
  if ((int)(unsigned const char)300 != 44) return 'n';
  if ((int)(signed const char)200 != -56) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
