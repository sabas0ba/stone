/* ファイル I/O と未実装 syscall の検査 (docs/stage012-os.md 8 章 第 2 部)
 *
 * syscall スタブは 'E' 前置部が提供する。openat の第 1 引数は AT_FDCWD
 * (-100) を渡すが，名前空間はフラットなのでカーネルは使わない (4.2)。
 */
int putc(int c);
int openat(int dirfd, char *path, int flags, int mode);
int read(int fd, char *buf, int n);
int write(int fd, char *buf, int n);
int close(int fd);
int brk(int addr);

int main(void) {
  int fd;
  int n;
  int i;
  char buf[64];

  /* 既存のファイルを読む */
  fd = openat(-100, "data.txt", 0, 0);
  if (fd < 0) { putc('E'); return 1; }
  n = read(fd, buf, 64);
  for (i = 0; i < n; i++) putc(buf[i]);
  close(fd);

  /* 新しいファイルを作って書く (ホスト側が内容を照合する) */
  fd = openat(-100, "new.txt", 65, 0);          /* O_CREAT | O_WRONLY */
  if (fd < 0) { putc('C'); return 1; }
  write(fd, "made\n", 5);
  close(fd);

  /* 無いファイルは -ENOENT */
  if (openat(-100, "none.txt", 0, 0) == -2) putc('N');
  /* 閉じた fd は -EBADF */
  if (read(fd, buf, 1) == -9) putc('B');
  /* brk(0) は現在のブレークを返す */
  if (brk(0) != 0) putc('K');
  putc('\n');
  return 0;
}
