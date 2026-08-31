/* 整数定数の型 (cc15n)。C89 6.1.3.2 は「値が収まる最初のもの」と定める
 * (int -> unsigned int -> ...)。0x80000000 は signed int に収まらない
 * ので unsigned int であり，64 bit へは**零拡張**される。
 *
 * 32 bit に閉じている限り bit の並びは同じなので表に出ないが，tcc は
 * 32 bit の定数を 64 bit の欄へ符号拡張するのにこの形を使うため，
 * 誤ると定数が黙って変わる (docs/stage015-tcc.md 12.14)。 */
typedef unsigned long long u64;
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }
int main() {
  u64 l;
  l = 0xFFFFFFFFFFFFFFFFULL;
  chk('a', (l & 0x80000000) == 0x80000000ULL);        /* 零拡張が正しい */
  chk('b', (l & 0x7fffffff) == 0x7fffffffULL);        /* 正の値は元から int */
  chk('c', (l & 0xffffffff) == 0xffffffffULL);        /* これも unsigned int */
  chk('d', -(l & 0x80000000) == 0xFFFFFFFF80000000ULL);
  chk('e', ((unsigned)l | -(l & 0x80000000)) == 0xFFFFFFFFFFFFFFFFULL);
  l = 0x80000000;                                      /* 代入でも */
  chk('f', l == 0x80000000ULL);
  l = 4294967295;                                      /* 10 進でも同じ */
  chk('g', l == 0xFFFFFFFFULL);
  putc(10);
  return 0;
}
