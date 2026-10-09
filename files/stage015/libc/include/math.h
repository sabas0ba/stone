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

#endif
