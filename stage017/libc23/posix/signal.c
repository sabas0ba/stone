/* signal.c --- シグナルの受け口 (第 21 世代。**起こさない**)
 *
 * 設計は include/signal.h の註。我々の OS にシグナルは無いので，
 * 登録は受け付けて何も起こさない。
 *
 * **表は持たない。** 持っても誰も引かないので，持つと「効いている
 * ように見える」だけである。返り値は常に SIG_DFL (直前の扱い) とする。
 */
#include <errno.h>
#include <signal.h>

__sighandler_t signal(int sig, __sighandler_t func) {
  if (sig < 1 || sig > 15) {
    errno = EINVAL;
    return SIG_ERR;
  }
  /* func は捨てる。呼ぶ道が無い */
  return SIG_DFL;
}

/* 自分へ送る。**送れない**ので拒む。ここで 0 を返すと「送ったのに
 * 何も起きない」という嘘になる */
int raise(int sig) {
  errno = EINVAL;
  return -1;
}
