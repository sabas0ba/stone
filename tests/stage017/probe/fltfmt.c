/* 浮動小数点の変換 (%e / %f / %g) を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 6.3)。
 *
 * `printfmt` は整数と文字列の変換を見ている。ここが見るのは**浮動小数点
 * の 3 つの形**である —— 第 22 世代は `%g` と `%e` を `%f` と同じに扱い，
 * 丸めを 0 から遠い側へ倒していた。どちらも落ちるのではなく**黙って
 * 違う値を書く**ので，突き合わせでしか出ない。
 *
 * ## 何を測らないか
 *
 * 有効 17 桁を求める形と，`%f` で整数部と小数部を合わせて 18 桁を
 * 超える形は測らない。我々の変換は 2 進の値を十進へ**正確に**展開せず，
 * 10 の冪で寄せてから数えるので，そこから先はホストと分かれる
 * (docs/stage017-gcc.md 6.3 に限界を書いてある)。実際の呼び手が使うのは
 * `%.6g` や `%.2f` の類で，そこは一致する。 */
#include <stdio.h>

static void one(char *tag, double v) {
  printf("%s %e\n", tag, v);
  printf("%s %f\n", tag, v);
  printf("%s %g\n", tag, v);
  printf("%s %.0f %.1f %.2f %.3f\n", tag, v, v, v, v);
  printf("%s %.0e %.1e %.3e %.6e\n", tag, v, v, v, v);
  printf("%s %.1g %.3g %.6g %.10g\n", tag, v, v, v, v);
  printf("%s %E %G\n", tag, v, v);
  printf("%s [%12.4f][%-12.4f][%012.4f]\n", tag, v, v, v);
  printf("%s [%+.3f][% .3f][%#.0f]\n", tag, v, v, v);
  printf("%s [%12.4e][%-12.4e][%012.4e]\n", tag, v, v, v);
  printf("%s [%12.4g][%-12.4g][%012.4g]\n", tag, v, v, v);
}

/* NaN を書式化して**符号を落として**出す (上の註) */
static void nanfmt(char *f, double v)
{
    char b[64];
    char *p;
    snprintf(b, sizeof b, f, v);
    p = b;
    if (*p == '-') p = p + 1;
    printf("nan %s|", p);
}

int main(void) {
  /* 半端の倒し方が出る値。ホストの printf は偶数側へ倒す */
  one("half0", 0.5);
  one("half1", 1.5);
  one("half2", 2.5);
  one("half3", 3.5);
  one("q245", 2.45);
  one("q235", 2.35);
  one("q0125", 0.125);

  /* 普通の値 */
  one("zero", 0.0);
  one("one", 1.0);
  one("neg", -1.0);
  one("pi", 3.14159265358979);
  one("third", 1.0 / 3.0);
  one("tenth", 0.1);

  /* %g が指数の形へ移る境 (指数が -4 より小さいか，精度以上) */
  one("e5", 1e5);
  one("e6", 1e6);
  one("em4", 1e-4);
  one("em5", 1e-5);
  one("big", 123456789.0);
  one("small", 0.000123456);
  one("e15", 1e15);
  one("em15", 1e-15);

  /* 末尾の 0 を落とすか (%g の要点) */
  one("t1", 100.0);
  one("t2", 1.5);
  one("t3", 0.0001);
  one("t4", 1250000.0);

  /* **無限と NaN** (Codex の指摘。libc23 の fpspecial)。
   * 桁寄せの輪は無限では終わらないので，入る前に捌かなければ
   * 走行ごと止まる。ホストは inf / -inf / nan と出す */
  {
    double inf;
    double nan;
    inf = 1e308 * 10.0;
    nan = inf - inf;
    printf("inf %f|%e|%g|%F|%E|%G\n", inf, inf, inf, inf, inf, inf);
    printf("ninf %f|%e|%g\n", 0.0 - inf, 0.0 - inf, 0.0 - inf);
    /* **NaN の符号は比べない。** IEEE 754 は `inf - inf` が返す NaN の
     * 符号を規定していない —— ホスト (x86-64) は符号 bit を立てて
     * `-nan` と出し，我々の実行時支援は立てない。**書式の話ではなく
     * 算術の話**なので，ここは物差しにならない (5.1 の compile flags と
     * 同じ筋)。先頭の '-' を落として突き合わせる */
    nanfmt("%f", nan); nanfmt("%e", nan);
    nanfmt("%g", nan); nanfmt("%F", nan);
    printf("\n");
    printf("infw |%12f|%-12e|\n", inf, inf);
  }

  /* **大きな精度** (Codex の指摘)。器の外へ書かないこと。
   * 有効 18 桁より先は 0 で埋める約束なので，そこはホストと分かれる
   * —— ここでは 0.0 と 2 の冪だけを見る (どちらも正確に表せる) */
  printf("p20z %.20e|%.20f\n", 0.0, 0.0);
  printf("p20h %.20e\n", 0.5);
  printf("p23z %.23e|%.23f\n", 0.0, 0.0);

  return 0;
}
