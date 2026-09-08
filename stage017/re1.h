/* re1.h --- 正規表現機構の第 1 世代 (docs/stage017-gcc.md 5.6)
 *
 * POSIX BRE の部分集合。実装は re1.c で，**sed と grep が同じものを
 * 使う**。写しを 2 つ持つと必ず片方だけ直す誤りが出る —— この鎖で
 * 何度も踏んだ形である (stage018-ext.md 3.2 / 5.1)。
 *
 * 受ける形:  ^ $ . *  [...]  [^...]  [a-z]  \( \)  \1〜\9  \+  \?
 *            \. \\ \n \t
 * 受けない形: \{n,m\}  \|  (名指しで拒む)
 *
 * 照合は後戻りで，**最左最長ではなく最左・貪欲**である。選択を受けない
 * ので，受ける形の範囲では POSIX と同じ結果になる。
 */
#ifndef RE1_H
#define RE1_H

/* 組む。成功なら先頭の節番号 (0 以上)，誤りなら -1 を返す。
 * 誤りの理由は re_errmsg() で読む */
int re_compile(char *pat);

/* 行のどこかに合うか。合えば 1 を返し，*mb / *me に範囲を入れる */
int re_search(int head, char *line, char **mb, char **me);

/* 途中から探す。**行頭を別に渡す** —— ^ の判定は行の先頭を知らないと
 * できないので、探し始める位置とは分けて持つ (sed の置換が使う) */
int re_search_from(int head, char *linestart, char *from, char **mb, char **me);

/* 組 (\( \)) の n 番目が合った範囲。合っていなければ 0 */
char *re_gs(int n);
char *re_ge(int n);

/* 組んだ結果の入れ物を空にする (台本を読み直すとき) */
int re_reset(void);

/* 直前の誤りの理由 */
char *re_errmsg(void);

#endif
