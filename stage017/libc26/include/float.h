/* float.h --- 浮動小数点型の特性 (C89 5.2.4.2.2。第 26 世代)
 *
 * 我々の cc の float は IEEE 754 の単精度，double は倍精度である。
 * **long double は double と同じ 8 バイト**である (cc15 系。tcc の RV32 と
 * 揃えた)。したがって LDBL_* は DBL_* と同じ値を持つ。
 *
 * 演算は四捨五入 (最も近い偶数への丸め) で行う。ソフト浮動小数点
 * (rtfp) もハードウェアの F / D 拡張も同じである。
 *
 * GCC の前提の MPFR (3.1.6) は configure でこの header を必須とする
 * (docs/stage017-gcc.md 8.15)。
 */
#ifndef _FLOAT_H
#define _FLOAT_H

#define FLT_RADIX 2
#define FLT_ROUNDS 1

#define FLT_MANT_DIG 24
#define FLT_DIG 6
#define FLT_MIN_EXP (-125)
#define FLT_MIN_10_EXP (-37)
#define FLT_MAX_EXP 128
#define FLT_MAX_10_EXP 38
#define FLT_MAX 3.40282347e+38F
#define FLT_EPSILON 1.19209290e-07F
#define FLT_MIN 1.17549435e-38F

#define DBL_MANT_DIG 53
#define DBL_DIG 15
#define DBL_MIN_EXP (-1021)
#define DBL_MIN_10_EXP (-307)
#define DBL_MAX_EXP 1024
#define DBL_MAX_10_EXP 308
#define DBL_MAX 1.7976931348623157e+308
#define DBL_EPSILON 2.2204460492503131e-16
#define DBL_MIN 2.2250738585072014e-308

#define LDBL_MANT_DIG DBL_MANT_DIG
#define LDBL_DIG DBL_DIG
#define LDBL_MIN_EXP DBL_MIN_EXP
#define LDBL_MIN_10_EXP DBL_MIN_10_EXP
#define LDBL_MAX_EXP DBL_MAX_EXP
#define LDBL_MAX_10_EXP DBL_MAX_10_EXP
#define LDBL_MAX DBL_MAX
#define LDBL_EPSILON DBL_EPSILON
#define LDBL_MIN DBL_MIN

#endif
