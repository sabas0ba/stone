/* alloca.h --- 呼び手の関数が戻るまで使える領域 (第 26 世代)
 *
 * C89 の関数ではない (glibc / BSD の拡張)。glibc と同じく専用の header に
 * 置く。stdlib.h に置くと，自前で `char *alloca ();` を宣言する
 * ソース (GMP の gmp-impl.h) と型がぶつかる (docs/stage017-gcc.md 8.15)。
 *
 * **ヒープから取り**，次に呼ばれたとき呼び手より深い (既に戻った) 関数が
 * 取った分を返す (src/misc26.c)。真のスタック上の確保ではないが，戻った
 * 後に触らない限り同じに振る舞う。
 */
#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

void *alloca(size_t n);

#endif
