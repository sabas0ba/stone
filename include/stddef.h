/* stddef.h --- 共通の定義 (C89 7.1.6)
 *
 * size_t を unsigned にするのは，この処理系のポインタが 32 ビットで
 * unsigned も 32 ビットだからである (docs/stage011-libc.md 3.1)。
 *
 * NULL を ((void *)0) ではなく 0 にしているのは，void * との比較や
 * 代入で余計な型検査に引っかからないようにするためである。
 *
 * offsetof は「0 番地に置いた構造体のメンバのアドレス」という常套手段で
 * 書ける。メンバ参照がアドレス計算に落ちるので，実際に 0 番地を読む
 * ことはない。
 *
 * 制限: wchar_t は無い (ワイド文字は非目標)。
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned size_t;
typedef int ptrdiff_t;

#define NULL 0
#define offsetof(t, m) ((size_t)&(((t *)0)->m))

#endif
