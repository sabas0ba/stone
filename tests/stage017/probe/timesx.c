/* times / sysconf / pathconf / PATH_MAX を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 8.9)。
 *
 * 第 25 世代で足した。GCC 4.7.4 の libiberty/getruntime が times と
 * _SC_CLK_TCK を，libiberty/lrealpath が PATH_MAX と _PC_PATH_MAX を使う。
 *
 * ---- 何を突き合わせるか ----
 *
 * 時間の値そのものは実行ごとに違う。突き合わせるのは**欄が満たすべき
 * 性質**である ——
 *
 *   tick の単位が 100 Hz か (Linux の USER_HZ と同じ値)
 *   知らない名前を訊けば EINVAL で -1 になるか
 *   経路の上限が POSIX の最小値 (256) 以上か
 *   無い経路を訊けば ENOENT で -1 になるか
 *   時間が戻らないか
 *   計算を続ければ CPU 時間が増えるか
 *
 * 0 を返すだけの times は最後の性質で落ちる。 */
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/times.h>

/* y / n で答える。値ではなく**性質**を出す */
static void yn(char *tag, int v) { printf("%s %s\n", tag, v ? "y" : "n"); }

/* 最適化で消されない計算。戻り値を使う */
static unsigned spin(unsigned x, int n) {
  int i;
  for (i = 0; i < n; i++) x = x * 1103515245 + 12345;
  return x;
}

int main(void) {
  struct tms a;
  struct tms b;
  clock_t ra;
  clock_t rb;
  long cpu0;
  long cpu;
  unsigned acc;
  int rounds;

  printf("clk %ld\n", sysconf(_SC_CLK_TCK));

  errno = 0;
  yn("sc-unknown", sysconf(-12345) == -1 && errno == EINVAL);

  yn("path-max-macro", PATH_MAX >= 256);
  yn("path-max-root", pathconf("/", _PC_PATH_MAX) >= 256);
  errno = 0;
  yn("path-noent", pathconf("tx_no_such_file", _PC_PATH_MAX) == -1
                   && errno == ENOENT);

  ra = times(&a);
  yn("times-ok", ra != (clock_t)-1);

  /* CPU 時間が 5 tick (50 ms) 増えるまで計算する。打ち切りは経過
   * 1000 tick (10 秒) */
  cpu0 = (long)(a.tms_utime + a.tms_stime);
  cpu = cpu0;
  acc = 1;
  rounds = 0;
  rb = ra;
  while (cpu - cpu0 < 5 && rb - ra < 1000) {
    acc = spin(acc, 100000);
    rb = times(&b);
    cpu = (long)(b.tms_utime + b.tms_stime);
    rounds = rounds + 1;
  }
  yn("cpu-grows", cpu - cpu0 >= 5);
  yn("monotonic", rb >= ra && b.tms_utime >= a.tms_utime
                  && b.tms_stime >= a.tms_stime);
  yn("no-children", b.tms_cutime == 0 && b.tms_cstime == 0);
  /* acc を使う (消されないように)。値は出さない */
  yn("spun", rounds > 0 && acc != 0);
  return 0;
}
