/* mk.c --- ビルド記述を読んで実行する (Stage 13 第 4 部)
 *
 * 設計は docs/stage013-tools.md 9 章。目標 (target)・依存・命令の並びを
 * 書いた記述を読み，依存を先に済ませてから命令を順に実行する。
 *
 *   mk [-f 記述] [目標]
 *
 * 記述の形は make に似せた。
 *
 *   # 行頭の # は注釈
 *   all: cc pp          <- 目標行 (行頭が空白でなく，': ' を含む)
 *   	echo done         <- 命令行 (行頭が空白)
 *
 * **時刻で作り直しの要否を決めることはしない。** sfs はファイルの時刻を
 * 持たないからである (docs/stage012-os.md 4.3)。mk がするのは
 * 「依存を先に，1 度だけ」という順序づけであって，古さの判定ではない
 * (docs/stage013-tools.md 9.2)。
 *
 * 命令行はシェルと同じ形で解釈する (空白で分割，`< 名前` `> 名前` で
 * 標準入出力を結ぶ)。組込みは echo だけである。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define NTGT  64                /* 目標の数 */
#define NCMD  512               /* 命令行の総数 */
#define NDEP  256               /* 依存の総数 */
#define TXT   32768             /* 記述の大きさ */

char txt[32768];                /* 記述の実体 (行ごとに NUL を置く)。TXT */
int txtn;

char *tname[64];                /* 目標の名前。NTGT */
int tdep0[64];                  /* 依存の並びの開始 (dep[] 内) */
int tdepn[64];
int tcmd0[64];                  /* 命令の並びの開始 (cmd[] 内) */
int tcmdn[64];
int tdone[64];                  /* 1 = 済み，2 = 実行中 (循環の検出) */
int ntgt;

char *dep[256];                 /* 依存の名前。NDEP */
int ndep;
char *cmd[512];                 /* 命令行 (書き換えるので実体は txt 内)。NCMD */
int ncmd;

char line[256];                 /* 命令行の作業用の写し */

/* 名前で目標を引く。無ければ -1 */
int find(char *s) {
  int i;
  for (i = 0; i < ntgt; i++)
    if (strcmp(tname[i], s) == 0) return i;
  return -1;
}

/* 記述を読み込んで表を作る。誤りなら -1 */
int load(char *path) {
  FILE *f;
  char *p;
  char *q;
  int c;
  int n;

  f = fopen(path, "r");
  if (f == NULL) return -1;
  n = 0;
  for (;;) {
    c = fgetc(f);
    if (c == EOF) break;
    if (n >= TXT - 1) { fclose(f); return -1; }
    txt[n] = c;
    n = n + 1;
  }
  fclose(f);
  txt[n] = 0;
  txtn = n;

  /* 行に切って表へ入れる */
  p = txt;
  while (*p) {
    q = strchr(p, '\n');
    if (q != NULL) *q = 0;
    if (p[0] == '#' || p[0] == 0) {
      /* 注釈と空行 */
    } else if (p[0] == ' ' || p[0] == '\t') {
      /* 命令行。属する目標がまだ無ければ誤り */
      while (*p == ' ' || *p == '\t') p++;
      if (*p != 0 && *p != '#') {
        if (ntgt == 0 || ncmd >= NCMD) return -1;
        cmd[ncmd] = p;
        ncmd = ncmd + 1;
        tcmdn[ntgt - 1] = tcmdn[ntgt - 1] + 1;
      }
    } else {
      /* 目標行 */
      char *colon;
      colon = strchr(p, ':');
      if (colon == NULL) return -1;
      *colon = 0;
      if (ntgt >= NTGT) return -1;
      tname[ntgt] = p;
      tdep0[ntgt] = ndep;
      tdepn[ntgt] = 0;
      tcmd0[ntgt] = ncmd;
      tcmdn[ntgt] = 0;
      ntgt = ntgt + 1;
      /* 依存を空白で切る */
      p = colon + 1;
      for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '#') break;
        if (ndep >= NDEP) return -1;
        dep[ndep] = p;
        ndep = ndep + 1;
        tdepn[ntgt - 1] = tdepn[ntgt - 1] + 1;
        while (*p != 0 && *p != ' ' && *p != '\t') p++;
        if (*p != 0) { *p = 0; p++; }
      }
    }
    if (q == NULL) break;
    p = q + 1;
  }
  return 0;
}

/* 命令行を 1 本実行する。子の終了コード (誤りは -1) */
int runcmd(char *s) {
  char *av[9];
  char *in;
  char *out;
  char *p;
  char *t;
  int ac;
  int mode;
  int i;

  if ((int)strlen(s) > 254) return -1;
  strcpy(line, s);
  ac = 0;
  in = NULL;
  out = NULL;
  mode = 0;
  p = line;
  for (;;) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0) break;
    t = p;
    while (*p != 0 && *p != ' ' && *p != '\t') p++;
    if (*p != 0) { *p = 0; p++; }
    if (mode == 1) { in = t; mode = 0; }
    else if (mode == 2) { out = t; mode = 0; }
    else if (strcmp(t, "<") == 0) mode = 1;
    else if (strcmp(t, ">") == 0) mode = 2;
    else if (ac < 8) { av[ac] = t; ac = ac + 1; }
  }
  if (mode != 0 || ac == 0) return -1;
  av[ac] = NULL;

  /* 進み具合が判るように，実行する前に出す (make の作法) */
  puts(s);

  if (strcmp(av[0], "echo") == 0) {
    for (i = 1; i < ac; i++) {
      fputs(av[i], stdout);
      if (i + 1 < ac) putchar(' ');
    }
    putchar('\n');
    return 0;
  }
  return spawn(av[0], av, in, out);
}

/* 目標を作る。0 = 成功 */
int make(char *name) {
  int t;
  int i;
  int r;

  t = find(name);
  if (t < 0) {
    fputs("mk: no target ", stderr);
    fputs(name, stderr);
    fputs("\n", stderr);
    return 1;
  }
  if (tdone[t] == 1) return 0;
  if (tdone[t] == 2) {
    fputs("mk: cycle at ", stderr);
    fputs(name, stderr);
    fputs("\n", stderr);
    return 1;
  }
  tdone[t] = 2;
  for (i = 0; i < tdepn[t]; i++) {
    r = make(dep[tdep0[t] + i]);
    if (r != 0) return r;
  }
  for (i = 0; i < tcmdn[t]; i++) {
    r = runcmd(cmd[tcmd0[t] + i]);
    if (r != 0) {
      if (r < 0) fputs("mk: bad command\n", stderr);
      else printf("mk: %s: exit %d\n", name, r);
      return 1;
    }
  }
  tdone[t] = 1;
  return 0;
}

int main(int argc, char **argv) {
  char *path;
  char *goal;
  int i;

  path = "mkfile";
  goal = NULL;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
      i = i + 1;
      path = argv[i];
    } else {
      goal = argv[i];
    }
  }
  if (load(path) < 0) {
    fputs("mk: cannot read ", stderr);
    fputs(path, stderr);
    fputs("\n", stderr);
    return 2;
  }
  if (ntgt == 0) {
    fputs("mk: no target\n", stderr);
    return 2;
  }
  if (goal == NULL) goal = tname[0];    /* 既定は最初の目標 */
  return make(goal);
}
