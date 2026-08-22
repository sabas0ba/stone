/* mkhost.c --- mk17 をホストで走らせるための身代わり。
 *
 * mk17 は我々の OS の spawn2 で sh2 を起こす。ホストにはどちらも無い
 * ので，spawn2 を system() へ回す殻をかぶせて **mk17.c そのものを**
 * 取り込む。**mk17.c の側に検査用の分岐を入れない**ためである
 * (入れると，ホストで通る道と OS で通る道が分かれてしまう)。
 *
 *   cc -w -o mkhost tests/stage017/host/mkhost.c
 *
 * 走らせるときの命令は /bin/sh へ行く。mk17 が渡すのは
 * 「一時ファイルの名前」なので，sh がそれを読む形になり，OS の上で
 * sh2 が読むのと同じ形である。
 */
#include <stdlib.h>
#include <string.h>

static int spawn2(char *path, char **argv, char *in, char *out, char *err);

#include "../../../stage017/mk17.c"

static int spawn2(char *path, char **argv, char *in, char *out, char *err) {
  char cmd[1024];
  int r;
  (void)path;
  (void)in;
  (void)out;
  (void)err;
  strcpy(cmd, "sh ");
  strcat(cmd, argv[1]);
  r = system(cmd);
  if (r == -1) return 127;
  /* WEXITSTATUS 相当。<sys/wait.h> を引かずに済ませる */
  return (r >> 8) & 0xff;
}
