/* sys/stat.h --- ディレクトリの作成 (POSIX。第 16 世代)
 *
 * 実装は posix/sys.c。カーネルの mkdirat (34) を包む
 * (docs/stage016-os.md 7.2)。RV32 Linux に旧形式の mkdir は無い。
 *
 * **stat / fstat は無い。** sfs2 に許可・所有者・時刻が無く，返せる欄が
 * ほとんど 0 になる。「あるのに嘘の値を返す」より「無い」ほうが呼び手に
 * とって安全である (適合台帳でいう bad を作らない)。ファイルの長さは
 * lseek(fd, 0, SEEK_END) で取れる。
 *
 * mode は受けて捨てる。sfs2 に許可の概念が無い。
 */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

int mkdir(char *path, int mode);

#endif
