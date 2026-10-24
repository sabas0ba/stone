/* math.h --- 数学関数 (第 4 部の実測ぶんだけ)
 *
 * tcc が使うのは ldexp (浮動小数点リテラルの解析) と fabs 程度である。
 * long double は double と同じ 8 バイトなので ldexpl は ldexp になる。
 */
#ifndef _MATH_H
#define _MATH_H

double ldexp(double d, int n);
double fabs(double d);
#define ldexpl ldexp
#define HUGE_VAL (1e308 * 10.0)

/* ---- 第 26 世代 (docs/stage017-gcc.md 8.15) ---- */
/* x = m * 2^e, 0.5 <= |m| < 1 に分ける (C89 7.5.4.2)。0 / 無限大 / NaN は e = 0 */
double frexp(double x, int *e);

#endif
