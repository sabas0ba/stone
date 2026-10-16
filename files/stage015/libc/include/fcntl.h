/* fcntl.h --- ファイルを開く (POSIX)
 *
 * 実装は lib/posix/sys.c。open は syscall の openat を包む
 * (RV32 には旧形式の open が無い。docs/stage012-os.md 5.4)。
 *
 * 値は Linux のものに合わせる。名前空間はフラットなので，カーネルは
 * ディレクトリ fd を使わない (docs/stage012-os.md 4.2)。
 */
#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  64
#define O_TRUNC  512

#define AT_FDCWD (-100)

/* 第 3 引数 (mode) は O_CREAT のとき渡される。sfs に許可は無いので
 * 受けて捨てる。tcc は 2 引数と 3 引数の両方で呼ぶ */
int open(char *path, int flags, ...);

#endif
