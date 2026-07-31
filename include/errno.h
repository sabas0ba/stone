/* errno.h --- エラー番号 (C89 7.1.4 / POSIX)
 *
 * syscall は失敗を負値 (-errno) で返す。libc の環境部 (lib/posix/sys.c) が
 * それを errno へ写し，関数は C の慣例どおりの値 (-1 や NULL) を返す。
 * 方針は docs/stage012-os.md 6.3。
 *
 * 値は Linux のものに合わせる (syscall ABI を Linux 互換にしているため。
 * docs/stage012-os.md 2.1)。当面必要なものだけを定義する。
 */
#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno;

#define EPERM   1
#define ENOENT  2
#define EBADF   9
#define ENOMEM  12
#define EACCES  13
#define EINVAL  22
#define ENOSPC  28
#define ERANGE  34
#define ENOSYS  38

#endif
