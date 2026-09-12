/* printf の変換指定を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.3)。
 *
 * ここまでの差分試験は前置部だけで走る形だったので，libc は測れて
 * いなかった。**libc の穴は我々が書いた期待値では出ない** —— 我々は
 * 自分が使う書き方しか試さないからである (cc15u と同じ構図)。
 *
 * 1 行 1 主題にし，**変換ごとに printf を分けてある**。可変部の
 * 取り出しに食い違いがあると，同じ printf の残りの引数がすべて
 * ずれるので，1 行に混ぜると原因が読めなくなる。
 *
 * 値は 32 bit に収まる範囲だけを使う。ホストの long は 64 bit なので，
 * long の幅そのものに依る値を置くと「違って当然」の行になる。 */
#include <stdio.h>

int main(void) {
  int n;

  printf("d      [%d] [%d] [%d]\n", 42, -42, 0);
  printf("dmin   [%d]\n", -2147483647 - 1);
  printf("i      [%i]\n", -7);
  printf("u      [%u] [%u]\n", 0u, 4294967295u);
  printf("x      [%x] [%x]\n", 48879, 4294967295u);
  printf("X      [%X]\n", 48879);
  printf("o      [%o] [%o]\n", 493, 0);
  printf("c      [%c]\n", 'z');
  printf("s      [%s]\n", "abc");
  printf("pct    [%%]\n");
  printf("width  [%5d] [%-5d] [%05d]\n", 42, 42, 42);
  printf("wstr   [%5s] [%-5s]\n", "ab", "ab");
  printf("wneg   [%5d] [%05d]\n", -42, -42);
  printf("wover  [%2d]\n", 12345);
  printf("prec   [%.3d] [%.0d]\n", 7, 0);
  printf("precs  [%.5s] [%.0s]\n", "abcdefg", "abc");
  printf("star   [%*d] [%-*d]\n", 6, 42, 6, 42);
  printf("stars  [%.*s]\n", 3, "abcdefg");
  printf("plus   [%+d] [%+d]\n", 42, -42);
  printf("space  [% d]\n", 42);
  printf("hash   [%#x] [%#o]\n", 48879, 493);
  printf("l      [%ld] [%lu]\n", (long)-7, (unsigned long)7);
  printf("lx     [%lx]\n", (unsigned long)48879);
  printf("ll     [%lld] [%llu]\n", -1234567890123LL, 1234567890123ULL);
  printf("llx    [%llx]\n", 1234567890123ULL);
  printf("f      [%f]\n", 1.5);
  printf("fneg   [%f]\n", -0.25);
  printf("fprec  [%.2f] [%.0f]\n", 1.25, 2.0);
  /* **ちょうど半分の値は置かない。** 丸めの向きは C が定めておらず
   * (C89 7.9.6.1 は「丸める」としか言わない)，ホストは偶数へ，我々は
   * 0 から遠い側へ丸める。ここで測りたいのは「丸めるかどうか」で
   * あって，どちらへ倒すかではない (stage017/libc22.md 4) */
  printf("fround [%.0f] [%.1f]\n", 2.6, 0.26);
  n = printf("ret    [%d%s]\n", 42, "xy");
  printf("retn   [%d]\n", n);
  return 0;
}
