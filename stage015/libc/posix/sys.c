/* sys.c --- libc の環境部: syscall の包みと errno (第 13 世代)
 *
 * 設計は docs/stage012-os.md 6 章。前置部が提供する生のスタブ
 * (sys_read / sys_write / sys_openat / sys_close / sys_brk) は
 * 失敗を負値 (-errno) で返す。ここで errno へ写し，C の慣例どおりの
 * 返り値 (-1 や (void *)-1) に直す。
 *
 * syscall ABI は RV32 Linux 互換なので (2.1)，この環境部は自作 OS でも
 * 実 Linux でもそのまま通じる。第 13 世代で足した spawn だけは独自の
 * 番号 (500) を使う。API としての spawn は実 Linux の上でも
 * clone / execve / wait4 で実装できるので，利用側の可搬性は保たれる
 * (docs/stage013-tools.md 3.2)。
 */
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* 前置部が提供する生のスタブ。sys_ecall は ld13 の 'E' が持つ汎用スタブで，
 * a7 = n, a0..a2 = a, b, c で ecall する (docs/stage013-tools.md 3.1) */
int sys_read(int fd, void *buf, int n);
int sys_write(int fd, void *buf, int n);
int sys_openat(int dirfd, char *path, int flags, int mode);
int sys_close(int fd);
int sys_brk(unsigned addr);
int sys_ecall(int n, int a, int b, int c);

#define SYS_LSEEK 62
#define SYS_SPAWN 500

int errno;

/* 負値なら errno へ写して -1 を返す。-4096 より下は errno ではない */
static int wrap(int r) {
  if (r < 0 && r > -4096) {
    errno = -r;
    return -1;
  }
  return r;
}

int open(char *path, int flags, ...) {
  /* mode (O_CREAT のときの第 3 引数) は読まずに捨てる。sfs に許可は
   * 無く，可変部を読まなくても呼出し規約上の害は無い (呼び手が積んで
   * 呼び手が下ろす) */
  /* ルート直下しか無いので，先頭の '/' の並びは剥がして裸の名前にする
   * (tcc が -I/ から "//tccdefs.h" の形の経路を作る。第 6 部の実測) */
  while (*path == '/')
    path = path + 1;
  return wrap(sys_openat(AT_FDCWD, path, flags, 0));
}

int read(int fd, void *buf, size_t n) {
  return wrap(sys_read(fd, buf, (int)n));
}

int write(int fd, void *buf, size_t n) {
  return wrap(sys_write(fd, buf, (int)n));
}

int close(int fd) {
  return wrap(sys_close(fd));
}

/* brk は「新しいブレーク」を返す Linux 生の仕様なので，sbrk はここで
 * 差分を取って包む。伸ばせなければ errno = ENOMEM で (void *)-1 */
void *sbrk(int n) {
  unsigned old;
  unsigned want;

  old = (unsigned)sys_brk(0);
  if (n == 0) return (void *)old;
  want = old + (unsigned)n;
  if ((unsigned)sys_brk(want) != want) {
    errno = ENOMEM;
    return (void *)-1;
  }
  return (void *)old;
}

/* path を起動して終わりを待ち，子の終了コードを返す。引数はまとめて
 * 1 つの表 {path, argv, in, out} で渡す (docs/stage013-tools.md 3.2) */
int spawn(char *path, char **argv, char *in, char *out) {
  char *sa[4];

  sa[0] = path;
  sa[1] = (char *)argv;
  sa[2] = in;
  sa[3] = out;
  return wrap(sys_ecall(SYS_SPAWN, (int)sa, 0, 0));
}

long lseek(int fd, long off, int whence)
{
    return (long)wrap(sys_ecall(SYS_LSEEK, fd, (int)off, whence));
}
