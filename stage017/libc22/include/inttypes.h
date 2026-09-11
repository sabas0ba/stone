/* inttypes.h --- 固定幅整数 (第 6 部の実測: tcc の elf.h が使う)
 *
 * ILP32 なので int が 32 bit，long long が 64 bit である。
 * PRI* の書式マクロはまだ要らないので置かない。 */
#ifndef INTTYPES_H
#define INTTYPES_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;

#endif
