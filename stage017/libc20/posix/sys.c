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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
#define SYS_SPAWN2 501
/* 第 16 世代 (docs/stage016-os.md 7.2) */
#define SYS_GETCWD   17
#define SYS_MKDIRAT  34
#define SYS_UNLINKAT 35
#define SYS_CHDIR    49
#define SYS_GETDENTS 61
#define SYS_STATAT   79         /* 第 19 世代 (docs/stage017-cc.md 11.5) */

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
  /* 第 15 世代までは先頭の '/' の並びをここで剥がしていた。名前空間が
   * 平らでルート直下しか無かったので正しかったが，**作業ディレクトリを
   * 持った瞬間に重大な誤りになる** —— "/inc/one.c" が "inc/one.c" に
   * なり，ルートではなく cwd から引かれる。「絶対経路を渡したのに，
   * 今いる場所によって別のファイルが開く」という黙って間違う壊れ方で
   * ある (docs/stage016-os.md 7.4)。
   *
   * 剥がす処理はもう要らない。入った理由は tcc が -I/ から
   * "//tccdefs.h" の形の経路を作ることだったが，第 1 部の walk が
   * 先頭と連続の '/' を読み飛ばすので，そのまま渡して正しく引ける */
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

/* spawn に標準エラーの行き先を足したもの (第 18 世代)。
 *
 * **番号を分けてあるのは記録の長さが違うから**である。500 は 4 語，
 * 501 は 5 語。同じ番号にすると，古い呼び手が積んだ 4 語の記録の後ろを
 * カーネルが読んでしまう (docs/stage016-os.md 10.5) */
/* ---- 助言的ロックと走行の番号 (第 20 世代。fcntl.h / unistd.h の註) ----
 *
 * どちらもカーネルの syscall を持たない。**持てないのではなく，
 * 我々の形では意味を持たない**からである。
 */

int fcntl(int fd, int cmd, void *arg) {
  struct flock *fl;
  /* 開いていない fd を黙って受けない。**受けると「ロックが取れた」と
   * 読める答を返してしまう。**
   *
   * 負値だけを弾くのでは足りない。999 のような「負でないが開いていない」
   * fd も来る。lseek を位置を動かさない形 (SEEK_CUR + 0) で叩いて確かめる
   * —— 副作用が無く，開いていなければカーネルが EBADF を返す。
   * 0/1/2 は常に開いているが，カーネルの lseek は端末を扱えないので
   * 弾かれる。ここで先に通しておく */
  if (fd < 0) { errno = EBADF; return -1; }
  if (fd > 2 && sys_ecall(SYS_LSEEK, fd, 0, 1) < 0) {
    errno = EBADF;
    return -1;
  }
  if (cmd == F_GETLK) {
    /* 問い合わせ。**答を書かなければ意味がない。** 競合相手が居ないので
     * 答は常に「誰も持っていない」である。書かずに 0 を返すと，呼ぶ側は
     * 自分が入れた l_type をそのまま読み，**衝突していると受け取る** */
    if (arg == 0) { errno = EINVAL; return -1; }
    fl = (struct flock *)arg;
    fl->l_type = F_UNLCK;
    return 0;
  }
  if (cmd == F_SETLK || cmd == F_SETLKW) {
    /* 競合相手が居ないので必ず取れる。ただし **依頼の形が壊れている
     * ものは受けない** —— 受けると，何も指していない依頼が通ったと
     * 呼ぶ側に読める。「知らないものは受けて捨てない」を，命令だけで
     * なく**中身にも**当てる */
    if (arg == 0) { errno = EINVAL; return -1; }
    fl = (struct flock *)arg;
    if (fl->l_type != F_RDLCK && fl->l_type != F_WRLCK
        && fl->l_type != F_UNLCK) {
      errno = EINVAL;
      return -1;
    }
    /* 0/1/2 は SEEK_SET / SEEK_CUR / SEEK_END である (値は stdio.h に
     * あるが，ここは stdio を読まないので数で書く) */
    if (fl->l_whence != 0 && fl->l_whence != 1 && fl->l_whence != 2) {
      errno = EINVAL;
      return -1;
    }
    return 0;
  }
  /* 知らない命令は受けて捨てない */
  errno = EINVAL;
  return -1;
}

int getpid(void) {
  return 1;
}

int spawn2(char *path, char **argv, char *in, char *out, char *err) {
  char *sa[5];

  sa[0] = path;
  sa[1] = (char *)argv;
  sa[2] = in;
  sa[3] = out;
  sa[4] = err;
  return wrap(sys_ecall(SYS_SPAWN2, (int)sa, 0, 0));
}

long lseek(int fd, long off, int whence)
{
    return (long)wrap(sys_ecall(SYS_LSEEK, fd, (int)off, whence));
}

/* ---- ディレクトリ (第 16 世代。docs/stage016-os.md 7 章) ---- */

int mkdir(char *path, int mode) {
  return wrap(sys_ecall(SYS_MKDIRAT, AT_FDCWD, (int)path, mode));
}

/* 経路の長さ・更新時刻・種別を取る (第 19 世代)。
 * カーネルは u32 4 語を書くので，そのまま struct stat の並びに合わせて
 * ある。**許可・所有者は無い** —— 持っていない欄は返さない (11.5) */
int stat(char *path, struct stat *st) {
  unsigned raw[4];
  int r;
  r = wrap(sys_ecall(SYS_STATAT, AT_FDCWD, (int)path, (int)raw));
  if (r < 0) return -1;
  st->st_size = (long)raw[0];
  st->st_mtlo = raw[1];
  st->st_mthi = raw[2];
  st->st_type = (int)raw[3];
  return 0;
}

/* a が b より新しければ 1。**上位語から見る** —— 下位だけ見ると
 * 桁上がりで逆転する (11.2) */
int newer(struct stat *a, struct stat *b) {
  if (a->st_mthi != b->st_mthi) return a->st_mthi > b->st_mthi;
  return a->st_mtlo > b->st_mtlo;
}

/* 第 16 世代までは src/misc15.c に「何もせず 0 を返す」偽物があった。
 * 「消したと言ったのに消えていない」は台帳でいう bad である
 * (docs/stage016-os.md 9.4) */
int unlink(char *path) {
  return wrap(sys_ecall(SYS_UNLINKAT, AT_FDCWD, (int)path, 0));
}

int chdir(char *path) {
  return wrap(sys_ecall(SYS_CHDIR, (int)path, 0, 0));
}

/* カーネルの getcwd は NUL を含む長さを返す。C の getcwd は buf を
 * 返すのが約束なので，ここで詰め替える */
char *getcwd(char *buf, size_t size) {
  if (wrap(sys_ecall(SYS_GETCWD, (int)buf, (int)size, 0)) < 0) return 0;
  return buf;
}

/* 生の getdents64。包み (opendir / readdir) は posix/dir.c にある */
int getdents64(int fd, void *buf, int n) {
  return wrap(sys_ecall(SYS_GETDENTS, fd, (int)buf, n));
}

/* 経路を絶対形に直し，. と .. と重なった / を畳む (第 17 世代)。
 *
 * **シンボリックリンクが無いので字句的な畳み込みで足りる。** 本来の
 * realpath は各段を実際に辿って解決するが，sfs2 にリンクは無いので
 * 結果は同じになる。
 *
 * resolved が NULL なら malloc して返す (GNU の拡張。tcc が使う)。
 * 器は 1024 バイト。溢れたら errno = ENAMETOOLONG で NULL */
#define _RPMAX 1024

char *realpath(char *path, char *resolved) {
  char tmp[_RPMAX];
  char *out;
  int n;
  int i;
  int st;
  int len;
  int seg;

  /* 1. 絶対形にする。相対なら作業ディレクトリを前に置く */
  if (path[0] == '/') {
    if (strlen(path) >= _RPMAX) { errno = ENAMETOOLONG; return 0; }
    strcpy(tmp, path);
  } else {
    if (getcwd(tmp, _RPMAX) == 0) return 0;
    n = (int)strlen(tmp);
    if (n > 1) { tmp[n] = '/'; n = n + 1; }     /* "/" のときは足さない */
    if (n + (int)strlen(path) >= _RPMAX) { errno = ENAMETOOLONG; return 0; }
    strcpy(tmp + n, path);
  }

  /* 2. 字句的に畳む。out へ「/名前」を積み，.. で 1 つ戻す */
  out = resolved;
  if (out == 0) {
    out = (char *)malloc(_RPMAX);
    if (out == 0) { errno = ENOMEM; return 0; }
  }
  len = 0;
  i = 0;
  while (tmp[i]) {
    while (tmp[i] == '/') i = i + 1;
    if (tmp[i] == 0) break;
    st = i;
    while (tmp[i] && tmp[i] != '/') i = i + 1;
    seg = i - st;
    if (seg == 1 && tmp[st] == '.') continue;   /* . は捨てる */
    if (seg == 2 && tmp[st] == '.' && tmp[st + 1] == '.') {
      while (len > 0 && out[len - 1] != '/') len = len - 1;  /* 名前を落とす */
      if (len > 0) len = len - 1;               /* 直前の '/' も落とす */
      continue;
    }
    out[len] = '/';
    len = len + 1;
    memcpy(out + len, tmp + st, (size_t)seg);
    len = len + seg;
  }
  if (len == 0) { out[0] = '/'; len = 1; }      /* すべて畳んだらルート */
  out[len] = 0;
  return out;
}
