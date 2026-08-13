/* setjmp.h --- 非局所ジャンプ (C89 7.6)
 *
 * **本物の巻き戻しはしない。** setjmp は 0 を返し (直行路として正しい)，
 * longjmp は報告して exit(1) する。
 *
 * tcc の longjmp はコンパイルエラーからの回復にだけ使われる。自己ホスト
 * (T1/T2/T3) がコンパイルするのは正しいソースだけなので，この経路は
 * 走らない。走ったときは黙って続けるのではなく大声で止まる (「通るが
 * 誤り」を作らないための，意図した割り切り)。本物が要ることが実測で
 * 判ったら，ld の前置部にレジスタ退避のスタブを足す形で入れ直す。
 */
#ifndef _SETJMP_H
#define _SETJMP_H

/* cc は配列の typedef を持たないので，jmp_buf は 1 語の整数にする。
 * 本物の巻き戻しをしないのでこれで足りる (中身は使われない) */
typedef int jmp_buf;

#define setjmp(env) ((env) = 0)
void longjmp(jmp_buf env, int val);

#endif
