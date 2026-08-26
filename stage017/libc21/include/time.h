/* time.h --- 時刻 (第 4 部)
 *
 * カーネルに時計は無い。time は常に 0 を返し，localtime は紀元の
 * 固定値を返す。**毎回同じ値になることは固定点 (T2 == T3) には好都合**
 * である (時刻が入ると出力が実行のたびに変わる)。
 */
#ifndef _TIME_H
#define _TIME_H

/* **見張りを付ける** —— sys/types.h も同じ型を持つ (第 21 世代) */
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

struct tm {
  int tm_sec; int tm_min; int tm_hour;
  int tm_mday; int tm_mon; int tm_year;
  int tm_wday; int tm_yday; int tm_isdst;
};

time_t time(time_t *t);
struct tm *localtime(time_t *t);

#endif
