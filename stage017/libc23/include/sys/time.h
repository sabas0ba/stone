/* sys/time.h --- gettimeofday (第 6 部の実測: tcc が -bench の計時に使う。
 * 時計が無いので time() と同じく 0 固定を返す) */
#ifndef SYS_TIME_H
#define SYS_TIME_H

struct timeval {
    long tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif
