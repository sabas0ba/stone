/* sys/times.h --- プロセスの時間 (第 25 世代)
 *
 * GCC 4.7.4 の libiberty/getruntime.c が読む。config.h が HAVE_TIMES を
 * 立てると，無条件に <sys/times.h> を含めて times() を呼ぶ
 * (docs/stage017-gcc.md 8.9)。
 *
 * **4 つの欄はどれも本当の値である。** カーネル (kernel27) がトラップの
 * 出入りで goldfish RTC を読み，走っていたプロセスへ時間を足している。
 * 単位は tick で，1 秒は sysconf(_SC_CLK_TCK) = 100 tick である。
 *
 * 限界が 1 つある。カーネルはタイマ割込みを使っていないので，syscall
 * を呼ばずに走り続けている間の utime は次の syscall で足される。
 * times() 自身が syscall なので，**呼んだ時点までの分は必ず入っている**。
 */
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

/* clock_t は time.h も持ちうるので見張りを揃える */
#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef long clock_t;
#endif

struct tms {
  clock_t tms_utime;            /* U モードで走っていた時間 */
  clock_t tms_stime;            /* このプロセスの syscall を処理していた時間 */
  clock_t tms_cutime;           /* 終わった子の utime + cutime */
  clock_t tms_cstime;           /* 終わった子の stime + cstime */
};

/* buf へ 4 つの時間を書き，起動からの tick 数を返す。
 * 失敗は (clock_t)-1 */
clock_t times(struct tms *buf);

#endif
