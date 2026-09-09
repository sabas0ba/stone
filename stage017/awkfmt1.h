/* awkfmt1.h --- awk の数と書式 (docs/stage017-gcc.md 5.8)
 *
 * `awk1.c` から切り出した部分である。切り出した理由は 2 つ。
 *
 *   1. **書式の仕事は「数と字を並べる」で閉じている。** awk の値
 *      (二面性を持つ入れ物) を持ち込まずに済むので，分けると両側とも
 *      短くなる。awk はこの鎖でいちばん大きなプログラムである。
 *   2. `%e` / `%f` / `%g` は **libc22 が持っていない形**である
 *      (`%g` を `%f` と同じに扱っている)。ここに集めておけば，
 *      libc23 がそのまま下敷きにできる (5.8 / 6.3)。
 *
 * 値の受け渡しは配列で行う。awk の値そのもの (二面性を持つ入れ物) を
 * ここへ持ち込まないためである —— 書式の仕事は「数と字を並べる」で
 * 閉じている。
 */
#ifndef AWKFMT1_H
#define AWKFMT1_H

/* 10 の冪の表を作る。使う前に 1 度だけ呼ぶ */
int awk_fmtinit(void);

/* 数を字にする。整数なら整数の形，そうでなければ fmt (CONVFMT / OFMT)
 * を通す。out は 512 バイト以上を渡すこと */
int awk_numstr(double d, char *fmt, char *out);

/* 書式に値を並べる。as[i] は字としての姿，an[i] は数としての姿，
 * aisnum[i] は「数として持っている値か」(%c の扱いが変わる) */
int awk_fmt(char *fmt, int argc, char **as, double *an, int *aisnum,
            char *out, int outmax);

#endif
