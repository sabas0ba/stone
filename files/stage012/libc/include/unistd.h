/* unistd.h --- syscall の薄い包み (POSIX)
 *
 * 実装は lib/posix/sys.c。前置部が提供する生のスタブ (sys_read など) を
 * 呼び，失敗 (-errno) を errno へ写して -1 を返す
 * (docs/stage012-os.md 6.3)。
 *
 * 非目標: fork / exec / dup / pipe (プロセスは Stage 13 の課題)。
 */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

int read(int fd, void *buf, size_t n);
int write(int fd, void *buf, size_t n);
int close(int fd);

/* 記憶域の末尾を n バイト伸ばし，伸ばす前の末尾を返す。失敗は (void *)-1 */
void *sbrk(int n);

#endif
