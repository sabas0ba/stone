/* 生の syscall 層の検査 (docs/stage012-os.md 8 章 第 2 部)
 *
 * 'E' 前置部が提供するスタブを直接呼ぶ。生の層なので戻り値は -errno の
 * ままであり，libc の環境部 (errno へ写す層) はリンクしない (6.3)。
 * POSIX の名前で使う側は user/libc.c が検査する。
 *
 * openat の第 1 引数は AT_FDCWD (-100) を渡すが，名前空間はフラットなので
 * カーネルは使わない (4.2)。
 */
int putc(int c);
int sys_openat(int dirfd, char *path, int flags, int mode);
int sys_read(int fd, char *buf, int n);
int sys_write(int fd, char *buf, int n);
int sys_close(int fd);
int sys_brk(int addr);

int main(void) {
  int fd;
  int n;
  int i;
  char buf[64];

  /* 既存のファイルを読む */
  fd = sys_openat(-100, "data.txt", 0, 0);
  if (fd < 0) { putc('E'); return 1; }
  n = sys_read(fd, buf, 64);
  for (i = 0; i < n; i++) putc(buf[i]);
  sys_close(fd);

  /* 新しいファイルを作って書く (ホスト側が内容を照合する) */
  fd = sys_openat(-100, "new.txt", 65, 0);          /* O_CREAT | O_WRONLY */
  if (fd < 0) { putc('C'); return 1; }
  sys_write(fd, "made\n", 5);
  sys_close(fd);

  /* 無いファイルは -ENOENT */
  if (sys_openat(-100, "none.txt", 0, 0) == -2) putc('N');
  /* 閉じた fd は -EBADF */
  if (sys_read(fd, buf, 1) == -9) putc('B');
  /* brk(0) は現在のブレークを返す */
  if (sys_brk(0) != 0) putc('K');
  putc('\n');
  return 0;
}
