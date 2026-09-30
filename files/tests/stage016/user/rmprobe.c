/* rmprobe.c --- kernel20 の削除と libc17 の realpath を OS の上で確かめる
 *
 * 第 4 部の 1 で塞いだ「黙って間違う」実装 2 件を見る
 * (docs/stage016-os.md 9.4)。
 *
 *   unlink    第 16 世代までは**何もせず 0 を返していた**。
 *             「消したと言ったのに消えていない」を捕まえる
 *   realpath  第 16 世代までは**複写するだけだった**。
 *             相対経路がそのまま返るのを捕まえる
 *
 * どちらも「返り値は正しいが結果が嘘」という形なので，**返り値ではなく
 * 結果を見る**。unlink は消した後に開けないことを，realpath は畳んだ
 * 経路の文字列そのものを確かめる。
 *
 * 出力は 1 行 1 件の「名札 期待 実測」。
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char buf[256];

/* 経路が開けるか。開けたら中身を buf へ置く */
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

void wint(char *label, int expect, int got) {
  printf("%s %d %d\n", label, expect, got);
}

void wstr(char *label, char *expect, char *got) {
  if (got == 0) got = "<null>";
  printf("%s %s %s\n", label, expect, got);
}

/* 経路が「開ける / 開けない」を見る。expect は 1 か 0 */
void wopen(char *label, char *path, int expect) {
  printf("%s %d %d\n", label, expect, slurp(path));
}

/* 中身を書いたファイルを作る。作れたら 0 */
int mkfile(char *path, char *text) {
  int fd;
  int n;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) return -1;
  n = write(fd, text, (int)strlen(text));
  close(fd);
  if (n != (int)strlen(text)) return -1;
  return 0;
}

int main(void) {
  char rp[256];
  char name[32];
  int i;
  int fd;
  int r;

  /* ---- 1. 消したら本当に消えている ---- */
  wint("mk-tmp", 0, mkfile("/tmp1.txt", "ONE\n"));
  wopen("open-before", "/tmp1.txt", 1);
  wint("unlink", 0, unlink("/tmp1.txt"));
  /* ここが要点。第 16 世代は unlink が 0 を返すのに開けたままだった */
  wopen("open-after", "/tmp1.txt", 0);
  wint("unlink-again", -1, unlink("/tmp1.txt"));
  wint("unlink-missing", -1, unlink("/nosuch.txt"));
  wint("unlink-dir", -1, unlink("/src"));

  /* ---- 2. 開いたまま消しても読める (一時ファイルの常套手段) ---- */
  wint("mk-tmp2", 0, mkfile("/tmp2.txt", "TWO\n"));
  fd = open("/tmp2.txt", O_RDONLY);
  wint("unlink-open", 0, unlink("/tmp2.txt"));
  r = 0;
  if (fd >= 0) {
    r = read(fd, buf, 255);
    close(fd);
  }
  if (r > 0) {
    while (r > 0 && buf[r - 1] == '\n') r = r - 1;
    buf[r] = 0;
  } else {
    strcpy(buf, "<fail>");
  }
  wstr("read-unlinked", "TWO", buf);
  wopen("open-unlinked", "/tmp2.txt", 0);

  /* ---- 3. 作っては消すを繰り返しても項目が尽きない ---- */
  r = 0;
  for (i = 0; i < 20; i++) {
    strcpy(name, "/loop.txt");
    if (mkfile(name, "x") < 0) { r = -1; break; }
    if (unlink(name) < 0) { r = -1; break; }
  }
  wint("loop-20", 0, r);

  /* ---- 4. realpath が経路を畳む ---- */
  wstr("rp-abs", "/inc/one.c", realpath("/inc/one.c", rp));
  wstr("rp-dotdot", "/inc/one.c", realpath("/src/../inc/one.c", rp));
  wstr("rp-dot", "/src/one.c", realpath("/src/./one.c", rp));
  wstr("rp-slashes", "/src/one.c", realpath("//src///one.c", rp));
  wstr("rp-root", "/", realpath("/", rp));
  wstr("rp-above-root", "/", realpath("/..", rp));

  /* 相対経路は作業ディレクトリを前に置く。**ここが第 16 世代との差**で，
   * 昔は "../one.c" がそのまま返っていた */
  wint("chdir-a", 0, chdir("/src/a"));
  wstr("rp-rel", "/src/a/two.c", realpath("two.c", rp));
  wstr("rp-rel-dotdot", "/src/one.c", realpath("../one.c", rp));
  wstr("rp-rel-deep", "/inc/one.c", realpath("../../inc/one.c", rp));
  wint("chdir-root", 0, chdir("/"));
  wstr("rp-from-root", "/src/one.c", realpath("src/one.c", rp));

  printf("done\n");
  return 0;
}
