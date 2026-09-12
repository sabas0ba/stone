/* **`a op= b` は `a = a op b` と同じ意味である** (C89 6.3.16.2)。
 * 除算・剰余・右シフトは符号で命令が変わるので，複合代入も二項演算と
 * 同じ規則で選ばなければならない。
 *
 * cc15t までは複合代入のトークン (o_asnb + b_*) から来る種別を
 * **そのまま**使っていた。並びが符号つき側 (b_div / b_rem / b_srl) なので
 *
 *     unsigned int a;  a %= 65521;   -> 符号つき剰余 (rem)
 *     int a;           a >>= 1;      -> 論理シフト (srl)
 *
 * になる。二項の道 (`b = a % c` / `b = a >> c`) は符号を見ていた。
 * **片方だけ直っていた**わけである。cc15u で揃えた。
 *
 * どちらも**通ってしまうが値が違う** —— 台帳で bad と呼ぶいちばん悪い
 * 状態である。表に出た形は zlib の adler32 で，
 *
 *     #define MOD(a) a %= BASE          / * adler32.c * /
 *     sum2 += adler;  ...  MOD(sum2);
 *
 * の `sum2` が 2^31 を超えるため符号つき剰余だと誤る。**下位 16 bit
 * (adler) は 2^31 を超えないので合っていた** —— 上位 16 bit だけが
 * 違う，という出方をした (docs/stage017-gcc.md 5.1)。
 *
 * 我々のソースは >>= / /= / %= を 1 つも使っていない。だから
 * **自分自身を組む限り永久に表に出ない誤り**だった。 */
int putc(int c);

int f(void) {
  unsigned int u;
  int i;
  unsigned int uc;
  int ic;
  unsigned int adler;
  unsigned int sum2;
  int k;

  /* ---- 符号なしの剰余・除算。2^31 を超える値で分かれる ---- */
  u = 0xF0000000u; u %= 65521u;
  if (u != 4306u) return 'n';
  u = 0xF0000000u; u /= 65521u;
  if (u != 61454u) return 'n';
  /* 二項の道と同じ値になること (こちらは前から合っていた) */
  uc = 0xF0000000u;
  if ((uc % 65521u) != 4306u) return 'n';
  if ((uc / 65521u) != 61454u) return 'n';

  /* 右辺が符号なしでも符号なしになる (通常の算術変換) */
  i = 0xF0000000; i /= 65521u;
  if ((unsigned int)i != 61454u) return 'n';

  /* ---- 符号つきの右シフト。負の値で分かれる ---- */
  i = -256; i >>= 4;
  if (i != -16) return 'n';
  i = -1; i >>= 1;
  if (i != -1) return 'n';
  ic = -256;
  if ((ic >> 4) != -16) return 'n';

  /* 符号なしの右シフトは論理シフトのまま */
  u = 0xF0000000u; u >>= 4;
  if (u != 0x0F000000u) return 'n';
  uc = 0xF0000000u;
  if ((uc >> 4) != 0x0F000000u) return 'n';

  /* ---- 壊していないこと ---- */
  i = -7; i /= 2;    if (i != -3) return 'n';   /* 0 方向へ丸める */
  i = -7; i %= 2;    if (i != -1) return 'n';
  i = 1; i <<= 4;    if (i != 16) return 'n';
  i = 6; i *= 7;     if (i != 42) return 'n';
  i = 6; i += 7;     if (i != 13) return 'n';
  i = 6; i -= 7;     if (i != -1) return 'n';
  i = 6; i &= 3;     if (i != 2) return 'n';
  i = 6; i |= 1;     if (i != 7) return 'n';
  i = 6; i ^= 3;     if (i != 5) return 'n';
  u = 100u; u -= 1u; if (u != 99u) return 'n';

  /* ---- adler32 の形そのもの ---- */
  adler = 1u;
  sum2 = 0u;
  k = 0;
  while (k < 5552) {
    adler += 255u;
    sum2 += adler;
    k = k + 1;
  }
  /* sum2 はここで 2^31 を超えている。**ここが分かれ目である** */
  if (sum2 < 2147483648u) return 'n';
  adler %= 65521u;
  sum2 %= 65521u;
  if (adler != 39820u) return 'n';
  if (sum2 != 61839u) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
