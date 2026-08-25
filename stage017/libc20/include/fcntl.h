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

/* ---- 助言的ロック (第 20 世代) ----
 *
 * tcc の lib/tcov.c が，被覆率の表を書くときに書込みロックを取る。
 *
 * **我々には競合相手が居ない。** 走行は逐次で，spawn は子の終わりを
 * 待つ (docs/stage013-tools.md 3.2)。助言的ロックは「他の走行と
 * 折り合う」ためのものなので，相手が居ないなら**成功を返すのが正しい
 * 振舞い**である。何もしないのではなく，何もすることが無い。
 *
 * 値は Linux のものに合わせる。 */
#define F_RDLCK  0
#define F_WRLCK  1
#define F_UNLCK  2
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7

struct flock {
  short l_type;
  short l_whence;
  long  l_start;
  long  l_len;
  int   l_pid;
};

/* 上のロックの命令だけを受ける。他は EINVAL で拒む —— 受けて捨てると，
 * 効いているつもりの指定が効かないまま通る。
 *
 * **開いていない fd は EBADF で拒む。** 負値だけでなく，999 のような
 * 「負でないが開いていない」ものも弾く (lseek で確かめる)。
 * 「相手が居ないから成功」は **開いている fd に対してだけ**正しい。
 *
 * **F_GETLK は答を書く。** 競合相手が居ないので答は常に
 * l_type = F_UNLCK である。書かずに成功を返すと，呼ぶ側は自分が入れた
 * l_type をそのまま読んで**衝突していると受け取る** */
int fcntl(int fd, int cmd, void *arg);

#endif
