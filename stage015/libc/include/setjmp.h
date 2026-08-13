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

typedef int jmp_buf[1];

#define setjmp(env) ((env)[0] = 0)
void longjmp(jmp_buf env, int val);

#endif
