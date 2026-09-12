/* dirprobe.c --- kernel18 のディレクトリ操作を OS の上から確かめる
 *
 * 第 1 部 (pathprobe) が「経路が引けるか」だったのに対し，ここは
 *   1. 木を見られるか        (opendir / readdir)
 *   2. 木を作れるか          (mkdir)
 *   3. 木の中で位置を持てるか (chdir / getcwd / . / ..)
 * を見る (docs/stage016-os.md 7 章)。
 *
 * とくに **abs-from-src** が要点である。cwd が /src のときに
 * "/inc/one.c" を開いて INC-ONE が出なければ，libc が絶対経路を相対に
 * 変えてしまっている (7.4)。第 15 世代の libc はまさにそれをしていた。
 *
 * 出力は 1 行 1 件の「名札 期待 実測」で，突き合わせは expected/ が持つ。
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char buf[256];

/* 経路を開いて中身 (末尾の改行を落としたもの) を buf へ置く。
 * 開けなければ 0，開けたら 1 を返す */
int slurp(char *path) {
  int fd;
  int r;
  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  r = read(fd, buf, 255);
  close(fd);
  if (r < 0) r = 0;
  while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) r = r - 1;
  buf[r] = 0;
  return 1;
}

void want(char *label, char *path, char *expect) {
  int ok;
  ok = slurp(path);
  if (expect == 0) expect = "enoent";
  if (!ok) printf("%s %s enoent\n", label, expect);
  else printf("%s %s %s\n", label, expect, buf);
}

/* いまの作業ディレクトリを期待と並べて出す */
void wcwd(char *label, char *expect) {
  char cb[256];
  if (getcwd(cb, 256) == 0) printf("%s %s <fail>\n", label, expect);
  else printf("%s %s %s\n", label, expect, cb);
}

/* 整数の結果を期待と並べて出す (0 = 成功，-1 = 失敗) */
void wint(char *label, int expect, int got) {
  printf("%s %d %d\n", label, expect, got);
}

/* ディレクトリの中身を「名前:種別」の形で並べる。順序は表の順で決まる
 * (sfs2 は追記なので pack した順になる) */
void wdir(char *label, char *path, char *expect) {
  DIR *d;
  struct dirent *e;
  char out[256];
  int n;

  out[0] = 0;
  n = 0;
  d = opendir(path);
  if (d == 0) {
    printf("%s %s <fail>\n", label, expect);
    return;
  }
  while ((e = readdir(d)) != 0) {
    if (n > 0) strcat(out, ",");
    strcat(out, e->d_name);
    if (e->d_type == DT_DIR) strcat(out, ":d");
    else strcat(out, ":f");
    n = n + 1;
  }
  closedir(d);
  if (n == 0) printf("%s %s -\n", label, expect);
  else printf("%s %s %s\n", label, expect, out);
}

int main(void) {
  int fd;
  int r;

  /* 1. 起動直後はルートにいる */
  wcwd("cwd-boot", "/");
  /* boot と dirprobe は検査の仕掛けそのもの。像に入っている以上，
   * 一覧に出るのが正しい */
  wdir("ls-root", "/", "inc:d,src:d,boot:f,dirprobe:f,top.txt:f");
  wdir("ls-src", "/src", "a:d,one.c:f");

  /* 2. 移動して相対経路で引く */
  wint("chdir-src", 0, chdir("/src"));
  wcwd("cwd-src", "/src");
  want("rel-from-src", "one.c", "SRC-ONE");
  want("dot-from-src", "./one.c", "SRC-ONE");

  /* 3. **絶対経路は cwd に影響されない** (7.4 の要点) */
  want("abs-from-src", "/inc/one.c", "INC-ONE");

  /* 4. .. で上がる */
  wint("chdir-a", 0, chdir("/src/a"));
  wcwd("cwd-a", "/src/a");
  want("dotdot", "../one.c", "SRC-ONE");
  want("dotdot2", "../../inc/one.c", "INC-ONE");
  wint("chdir-dotdot", 0, chdir(".."));
  wcwd("cwd-after-dotdot", "/src");

  /* 5. ルートの親はルート自身 */
  wint("chdir-root", 0, chdir("/"));
  wint("chdir-above-root", 0, chdir("/.."));
  wcwd("cwd-above-root", "/");

  /* 6. 作る */
  wint("mkdir-out", 0, mkdir("/out", 0777));
  wint("mkdir-again", -1, mkdir("/out", 0777));
  wdir("ls-out-empty", "/out", "-");
  fd = open("/out/f.txt", O_WRONLY | O_CREAT, 0666);
  if (fd < 0) r = -1;
  else {
    r = write(fd, "MADE\n", 5);
    close(fd);
  }
  wint("write-in-new", 5, r);
  want("read-in-new", "/out/f.txt", "MADE");
  wdir("ls-out", "/out", "f.txt:f");
  /* 作ったディレクトリへ移り，そこから相対で引く */
  wint("chdir-out", 0, chdir("/out"));
  want("rel-in-out", "f.txt", "MADE");
  wint("chdir-back", 0, chdir("/"));

  /* 7. 誤りは誤りとして返す */
  wint("chdir-missing", -1, chdir("/nosuch"));
  wint("chdir-file", -1, chdir("/top.txt"));
  wint("mkdir-under-file", -1, mkdir("/top.txt/x", 0777));
  wint("mkdir-dotdot", -1, mkdir("/src/..", 0777));
  fd = open("/src", O_RDONLY);
  if (fd < 0) r = -2;
  else {
    r = read(fd, buf, 16);              /* ディレクトリの read は拒む */
    close(fd);
  }
  wint("read-dir", -1, r);

  printf("done\n");
  return 0;
}
