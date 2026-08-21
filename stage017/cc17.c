/* cc17.c --- コンパイラの駆動役 (Stage 17 第 1 部の試作)
 *
 * 我々の OS の上で `cc -o out in.c` と書けるようにする。中で
 * pp16 -> cc15p -> ld16 を順に呼ぶ。
 *
 *   cc [-c] [-o OUT] [-W...] [-x c] [FILE | -]
 *
 * これが要るのは configure がコンパイラを**コマンドとして**呼ぶから
 * である (docs/stage016-os.md 11.7)。configure が外へ出た 6 回は
 * すべてこれだった。
 *
 * **束ねは自分で書く。** ホストの tools/bundle.sh とゲストの
 * stage013/bundle.c はどちらも「引数の綴りをそのまま名前にする」ので，
 * /include/stdio.h を渡すと名前が "/include/stdio.h" になり
 * #include <stdio.h> から引けない。sfs2 に階層ができた今，束ねる側は
 * 「置いてある場所」と「名乗る名前」を別に持つ必要がある。駆動役は
 * 両方を知っているので，ここで組むのが素直である。
 *
 * 段取り:
 *   1. /include の下を読んで束ねを作る (最後に翻訳単位を置く)
 *   2. pp16  で前処理
 *   3. cc15p で翻訳 (-c ならここまで)
 *   4. 'E' + .o の連結 + '\0' を組んで ld16 へ流す
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#define PP   "/bin/pp16"
#define CC1  "/bin/cc15p"
#define LD   "/bin/ld16"
#define INC  "/include"
#define LIB  "/lib"

/* 作業用の名前。**同じディレクトリに作る。** 我々の OS に /tmp は
 * 無く，configure は作業ディレクトリを掘って回るので，そこへ置く */
#define TB "_cc.b"
#define TI "_cc.i"
#define TO "_cc.o"
#define TL "_cc.l"

/* 大きな作業領域は大域に置く。cc の 1 関数のフレームは 2040 バイト
 * までで，超えると領域超過で落ちる (docs/dev-notes.md 4 章) */
char buf[8192];
char path[512];
char name[512];

static void die(char *m, char *a) {
  fputs("cc: ", stderr);
  fputs(m, stderr);
  if (a) {
    fputs(" ", stderr);
    fputs(a, stderr);
  }
  fputs("\n", stderr);
  exit(1);
}

/* from を fd へ丸ごと写す。戻り値は写したバイト数 (-1 = 開けない) */
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

/* ファイルの大きさ。-1 = 開けない */
static long fsize(char *p) {
  int s;
  long n;
  s = open(p, O_RDONLY);
  if (s < 0) return -1;
  n = lseek(s, 0, 2);
  close(s);
  return n;
}

/* 束ねに 1 つ足す。nm で名乗り，中身は from から取る */
static void member(int fd, char *nm, char *from) {
  long sz;
  char hdr[600];
  sz = fsize(from);
  if (sz < 0) die("cannot open", from);
  sprintf(hdr, "@%s %ld\n", nm, sz);
  write(fd, hdr, (int)strlen(hdr));
  if (copyinto(fd, from) != sz) die("short read", from);
}

/* /include とその下の sys/ を束ねへ入れる。**名乗る名前は
 * #include が書く綴りである** (sys/stat.h は "sys/stat.h") */
static void addheaders(int fd) {
  DIR *d;
  DIR *e;
  struct dirent *p;
  d = opendir(INC);
  if (d == 0) return;
  while ((p = readdir(d)) != 0) {
    if (p->d_type == DT_DIR) continue;
    strcpy(path, INC);
    strcat(path, "/");
    strcat(path, p->d_name);
    member(fd, p->d_name, path);
  }
  closedir(d);
  strcpy(path, INC);
  strcat(path, "/sys");
  e = opendir(path);
  if (e == 0) return;
  while ((p = readdir(e)) != 0) {
    if (p->d_type == DT_DIR) continue;
    strcpy(path, INC);
    strcat(path, "/sys/");
    strcat(path, p->d_name);
    strcpy(name, "sys/");
    strcat(name, p->d_name);
    member(fd, name, path);
  }
  closedir(e);
}

int main(int argc, char **argv) {
  int i;
  int fd;
  int conly;
  int st;
  char *out;
  char *src;
  char *av[4];
  DIR *d;
  struct dirent *p;

  conly = 0;
  out = 0;
  src = 0;
  for (i = 1; i < argc; i = i + 1) {
    char *a;
    a = argv[i];
    if (strcmp(a, "-c") == 0) { conly = 1; continue; }
    if (strcmp(a, "-o") == 0) {
      i = i + 1;
      if (i >= argc) die("-o needs an argument", 0);
      out = argv[i];
      continue;
    }
    /* -x c / -xc は「これは C である」と言っているだけなので落とす */
    if (strcmp(a, "-x") == 0) { i = i + 1; continue; }
    if (strncmp(a, "-x", 2) == 0) continue;
    /* 警告の選択肢は黙って受ける。**受けたことを言ってはいけない。**
     * configure は「警告文に出てこない選択肢は使える」と読む */
    if (a[0] == '-' && a[1] != 0) continue;
    src = a;                            /* "-" もここに来る */
  }
  if (src == 0) die("no input file", 0);
  if (out == 0) out = conly ? TO : "a.out";

  /* 標準入力から読む形 (`-xc -`)。configure が使う */
  if (strcmp(src, "-") == 0) {
    int n;
    fd = open("_cc0.c", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) die("cannot create", "_cc0.c");
    for (;;) {
      n = read(0, buf, sizeof buf);
      if (n <= 0) break;
      write(fd, buf, n);
    }
    close(fd);
    src = "_cc0.c";
  }

  /* 1. 束ね */
  fd = open(TB, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TB);
  write(fd, "#!stone-bundle\n", 15);
  addheaders(fd);
  member(fd, src, src);
  write(fd, "\004", 1);                 /* EOT。pp と cc はこれで止まる */
  close(fd);

  /* 2. 前処理 */
  av[0] = PP;
  av[1] = 0;
  st = spawn(PP, av, TB, TI);
  if (st != 0) { fputs("cc: preprocess failed\n", stderr); return st; }

  /* 3. 翻訳 */
  av[0] = CC1;
  av[1] = 0;
  st = spawn(CC1, av, TI, conly ? out : TO);
  if (st != 0) { fputs("cc: compile failed\n", stderr); return st; }
  if (conly) return 0;

  /* 4. リンク。'E' + .o の連結 + '\0' を組む。**翻訳単位を先頭に置く**
   * (ld16 は最初の目的ファイルから入口を取る)。/lib には 1 揃いだけ
   * 置くこと —— 同じものの別実装が並ぶと ld が多重定義で落ちる */
  fd = open(TL, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TL);
  write(fd, "E", 1);
  if (copyinto(fd, TO) < 0) die("cannot open", TO);
  d = opendir(LIB);
  if (d == 0) die("cannot open", LIB);
  while ((p = readdir(d)) != 0) {
    int n;
    if (p->d_type == DT_DIR) continue;
    n = (int)strlen(p->d_name);
    if (n < 3 || strcmp(p->d_name + n - 2, ".o") != 0) continue;
    strcpy(path, LIB);
    strcat(path, "/");
    strcat(path, p->d_name);
    if (copyinto(fd, path) < 0) die("cannot open", path);
  }
  closedir(d);
  write(fd, "\0", 1);
  close(fd);

  av[0] = LD;
  av[1] = 0;
  st = spawn(LD, av, TL, out);
  if (st != 0) { fputs("cc: link failed\n", stderr); return st; }
  return 0;
}
