/* predef-rv32-gcc46.h --- Linux 5.4 を訳す処理系が前もって定義するマクロ
 * (docs/stage019-linux.md 6 章)
 *
 * 我々の処理系は GCC 4.6.4 と名乗る。Linux 5.4 が受ける最低の版で，新しい版を
 * 名乗るほど Linux は新しい拡張を使う (include/linux/compiler-gcc.h)。値は
 * riscv32 の GCC が -std=gnu89 -mabi=ilp32 -march=rv32ima -mcmodel=medlow -Os
 * で定義するものに合わせる。
 *
 * 今はホストの cpp に -undef と一緒に渡して，単位を前処理するのに使う
 * (tools/linux19.sh)。我々の pp と cc は，同じものを自分で定義することになる。
 *
 * 浮動小数点 (F / D) と圧縮命令 (C) は使わないので __riscv_flen と
 * __riscv_compressed は定義しない。整数の乗除算 (M) と不可分操作 (A) は
 * 定義する。A は inline asm とアセンブラが扱い，cc は生成しない。
 *
 * code model は medlow を既定とする。単位ごとに -mcmodel=medany を指定する
 * もの (arch/riscv/mm/init.o) では __riscv_cmodel_medany に替わる。
 */

/* 版 */
#define __GNUC__ 4
#define __GNUC_MINOR__ 6
#define __GNUC_PATCHLEVEL__ 4
#define __VERSION__ "4.6.4 (stone)"
#define __GNUC_GNU_INLINE__ 1
/* __STDC_HOSTED__ は cpp が自分で定義する */

/* 対象 */
#define __riscv 1
#define __riscv_xlen 32
#define __riscv_mul 1
#define __riscv_div 1
#define __riscv_muldiv 1
#define __riscv_atomic 1
#define __riscv_float_abi_soft 1
#define __riscv_cmodel_medlow 1
#define __ILP32__ 1
#define _ILP32 1
#define __ELF__ 1

/* -std=gnu89 の GNU/Linux の処理系は linux / unix も定義する */
#define __linux__ 1
#define __linux 1
#define __gnu_linux__ 1
#define linux 1
#define __unix__ 1
#define __unix 1
#define unix 1

/* 型の大きさと範囲 */
#define __CHAR_BIT__ 8
#define __CHAR_UNSIGNED__ 1
#define __SIZEOF_SHORT__ 2
#define __SIZEOF_INT__ 4
#define __SIZEOF_LONG__ 4
#define __SIZEOF_LONG_LONG__ 8
#define __SIZEOF_POINTER__ 4
#define __SIZEOF_SIZE_T__ 4
#define __SIZEOF_PTRDIFF_T__ 4
#define __SIZEOF_WCHAR_T__ 4
#define __SIZEOF_WINT_T__ 4
#define __SIZEOF_FLOAT__ 4
#define __SIZEOF_DOUBLE__ 8
#define __SIZEOF_LONG_DOUBLE__ 16
#define __SCHAR_MAX__ 127
#define __SHRT_MAX__ 32767
#define __INT_MAX__ 2147483647
#define __LONG_MAX__ 2147483647L
#define __LONG_LONG_MAX__ 9223372036854775807LL
#define __WCHAR_MAX__ 2147483647
#define __SIZE_TYPE__ unsigned int
#define __PTRDIFF_TYPE__ int
#define __WCHAR_TYPE__ int
#define __WINT_TYPE__ unsigned int
#define __INTMAX_TYPE__ long long int
#define __UINTMAX_TYPE__ long long unsigned int
#define __BIGGEST_ALIGNMENT__ 16

/* バイト順 */
#define __ORDER_LITTLE_ENDIAN__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#define __ORDER_PDP_ENDIAN__ 3412
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__

/* 最適化 (tiny.config は CC_OPTIMIZE_FOR_SIZE。-Os) */
#define __OPTIMIZE__ 1
#define __OPTIMIZE_SIZE__ 1
#define __FINITE_MATH_ONLY__ 0

/* 記号と不可分操作 */
#define __USER_LABEL_PREFIX__
#define __REGISTER_PREFIX__
#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 1
