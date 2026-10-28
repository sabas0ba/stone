/* alloca.h --- 呼び手の関数が戻るまで使える領域 (第 26 世代)
 *
 * C89 の関数ではない (glibc / BSD の拡張)。glibc と同じく専用の header に
 * 置く。stdlib.h に置くと，自前で `char *alloca ();` を宣言する
 * ソース (GMP の gmp-impl.h) と型がぶつかる (docs/stage017-gcc.md 8.15)。
 *
 * **ヒープから取る。** cc15ao 以降の cc は呼んだ関数の戻りで返すので，
 * 寿命は本物と同じである (src/misc26.c / stage015/cc15ao.sc)。
 */
#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

void *alloca(size_t n);

/* cc15ao 以降の cc が使う形 (src/misc26.c の註)。直接は呼ばない */
void *__alloca2(size_t n, char *fp);
int __alloca_release(char *fp);

#endif
