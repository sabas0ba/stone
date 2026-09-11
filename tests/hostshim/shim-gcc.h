/* 我々の pp が出した .i を host の処理系に読ませるための繋ぎ
 * (tools/gcc17.sh unit)。
 *
 * .i は我々の libc の header で前処理済みなので，host に含めさせる
 * header は無い。ただし我々の stdarg.h は，可変部の先頭をコンパイラが
 * 用意する隠しローカル __va_ptr で受ける (stage015/libc/include/stdarg.h)。
 * host にその名前は無いので，宣言だけ与える。
 *
 * **単位本体は 1 文字も変えない。** 変えたら「同じ .i を両方に読ませた」と
 * 言えなくなる (docs/stage017-gcc.md 5.2)。 */
extern char *__va_ptr;
