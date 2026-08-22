/* ar17.c --- 書庫 (Stage 17 第 2 部)
 *
 *   ar rcs ARCHIVE MEMBER...   作る / 差し替える
 *   ar t ARCHIVE               並びを出す
 *
 * tcc の Makefile が呼ぶのは `$(AR) rcs $@ $^` の 1 箇所だけである
 * (docs/stage017-cc.md 7.1)。`t` は我々の側の確認用に足した。
 *
 * **形式は本物に合わせる。** 独自形式にする手もあるが，本物に
 * 合わせておくと host の `ar t` / `ar x` を検査に使える (7.3)。
 * 身代わりを自分で書くと，自分の思い違いをそのまま固定してしまう。
 *
 *   "!<arch>\n" に続けて，員ごとに 60 バイトの頭 + 中身。
 *   中身は偶数境界へ '\n' で詰める。
 *
 *   位置  幅   中身
 *      0  16   名前 ('/' で終端し空白で詰める)
 *     16  12   更新時刻
 *     28   6   uid
 *     34   6   gid
 *     40   8   権限 (8 進)
 *     48  10   大きさ (10 進)
 *     58   2   "`\n"
 *
 * **バイト一致は狙わない。** 時刻や uid をどう書くかは実装ごとに違い，
 * そこを揃えることに意味は無い。狙うのは相互運用である。
 *
 * ---- 書かないもの ----
 *
 * **符号の索引 (`s`) は書かない。** 我々の cc は書庫の中身を全員
 * 並べるので索引を引かない。索引を書くには各員の ELF 符号表を読む
 * 必要があり，本題から遠い (7.4)。`s` は受け取って黙って無視する。
 * host の `ar t` / `ar x` は索引が無くても動く。
 *
 * **長い名前 (16 バイト以上) は扱わない。** 本物は `//` という特別な
 * 員に名前表を置いて `/offset` で参照するが，そこまでは作らない。
 * **黙って切り詰めず，はっきり失敗する。** 切り詰めると別の員と
 * 同じ名前になりうる。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAGIC "!<arch>\n"
#define MAGLEN 8
#define HDRLEN 60
#define NAMEMAX 15              /* '/' の分を除いた上限 */

/* 大きな作業領域は大域に置く (docs/dev-notes.md 4 章) */
char buf[8192];
char hdr[HDRLEN + 1];

static void die(char *m, char *a) {
  fputs("ar: ", stderr);
  fputs(m, stderr);
  if (a) {
    fputs(" ", stderr);
    fputs(a, stderr);
  }
  fputs("\n", stderr);
  exit(1);
}

/* 頭の欄を左詰めで埋める。余りは空白 */
static void field(int off, int width, char *s) {
  int i;
  int n;
  n = (int)strlen(s);
  for (i = 0; i < width; i = i + 1) hdr[off + i] = i < n ? s[i] : ' ';
}

static long fsize(char *p) {
  int fd;
  long n;
  fd = open(p, O_RDONLY);
  if (fd < 0) return -1;
  n = lseek(fd, 0, 2);
  close(fd);
  return n;
}

/* from の中身を fd へ写す */
static long copyinto(int fd, char *from) {
  int s;
  int n;
  long tot;
  s = open(from, O_RDONLY);
  if (s < 0) return -1;
  tot = 0;
  for (;;) {
    n = read(s, buf, sizeof buf);
    if (n <= 0) break;
    write(fd, buf, n);
    tot = tot + n;
  }
  close(s);
  return tot;
}

/* 員を 1 つ書く */
static void putmember(int fd, char *path) {
  long sz;
  char nm[64];
  char num[24];
  char *base;
  int i;

  /* 経路の最後の段を名前にする (本物と同じ) */
  base = path;
  for (i = 0; path[i]; i = i + 1)
    if (path[i] == '/') base = path + i + 1;

  if ((int)strlen(base) > NAMEMAX)
    die("member name too long (16 バイト以上は未対応。7.4)", base);

  sz = fsize(path);
  if (sz < 0) die("cannot open", path);

  strcpy(nm, base);
  strcat(nm, "/");
  field(0, 16, nm);
  field(16, 12, "0");           /* 更新時刻。0 で揃える (再現のため) */
  field(28, 6, "0");            /* uid */
  field(34, 6, "0");            /* gid */
  field(40, 8, "100644");       /* 権限 */
  sprintf(num, "%ld", sz);
  field(48, 10, num);
  hdr[58] = '`';
  hdr[59] = '\n';
  write(fd, hdr, HDRLEN);

  if (copyinto(fd, path) != sz) die("short read", path);
  if (sz & 1) write(fd, "\n", 1);       /* 偶数境界へ詰める */
}

/* 頭から 10 進を読む (欄は空白で詰めてある) */
static long fieldnum(char *h, int off, int width) {
  long v;
  int i;
  v = 0;
  for (i = 0; i < width; i = i + 1) {
    char c;
    c = h[off + i];
    if (c < '0' || c > '9') break;
    v = v * 10 + (c - '0');
  }
  return v;
}

static int list(char *arch) {
  int fd;
  int n;
  char m[MAGLEN];
  fd = open(arch, O_RDONLY);
  if (fd < 0) die("cannot open", arch);
  if (read(fd, m, MAGLEN) != MAGLEN || memcmp(m, MAGIC, MAGLEN) != 0)
    die("not an archive", arch);
  for (;;) {
    long sz;
    int i;
    n = read(fd, hdr, HDRLEN);
    if (n <= 0) break;
    if (n != HDRLEN) die("truncated header", arch);
    sz = fieldnum(hdr, 48, 10);
    /* 名前は '/' まで */
    for (i = 0; i < 16; i = i + 1) {
      if (hdr[i] == '/' || hdr[i] == ' ') break;
      fputc(hdr[i], stdout);
    }
    fputc('\n', stdout);
    if (lseek(fd, (int)(sz + (sz & 1)), 1) < 0) break;
  }
  close(fd);
  return 0;
}

int main(int argc, char **argv) {
  char *mode;
  char *arch;
  int fd;
  int i;

  if (argc < 3) {
    fputs("usage: ar {rcs|t} archive [member...]\n", stderr);
    return 2;
  }
  mode = argv[1];
  arch = argv[2];

  if (strchr(mode, 't') && !strchr(mode, 'r')) return list(arch);

  if (!strchr(mode, 'r')) die("unsupported mode (rcs か t だけ)", mode);
  /* 'c' は「作るとき黙る」，'s' は索引。どちらも受けて無視する (7.4) */

  fd = open(arch, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", arch);
  write(fd, MAGIC, MAGLEN);
  for (i = 3; i < argc; i = i + 1) putmember(fd, argv[i]);
  close(fd);
  return 0;
}
