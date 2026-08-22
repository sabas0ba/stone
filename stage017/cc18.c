/* cc18.c --- コンパイラの駆動役 第 18 世代 (Stage 17 第 2 部)
 *
 * cc17 (第 1 部) の写しに**複数の翻訳単位・書庫・-I / -D** を足した
 * ものである (docs/stage017-cc.md 7 章)。
 *
 *   cc [-c] [-o OUT] [-I dir] [-Dname[=val]] [-Uname] [-W...] [-x c]
 *      入力...
 *
 * 入力は綴りで見分ける。
 *
 *   *.c   翻訳する
 *   *.o   そのままリンクへ回す
 *   *.a   書庫。**展開して員をリンクへ回す**
 *   -     標準入力を翻訳する
 *
 * ---- 設計の要 (7.2) ----
 *
 * **リンカにも前処理器にも手を入れない。**
 *
 * ld16 の入力は 'E' + .o の連結 + '\0' なので，書庫はこちらで展開して
 * 並べればよい。リンカは書庫という概念を知らないままでよく，凍結済みの
 * 世代に手が入らない。
 *
 * pp16 は引数を取らない (標準入力から束ねを読むだけ) ので，-I は
 * 「その階層のヘッダを束ねの員に足す」，-D / -U は「翻訳単位の頭に
 * #define / #undef を挿む」という形で表す。**与えられた順に並べる**
 * ——  コマンド行の順が意味を持つからである。
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

#define TB "_cc.b"
#define TI "_cc.i"
#define TL "_cc.l"

#define NIN   64                /* 入力の上限 */
#define NINC  16                /* -I の上限 */
#define NDEF  64                /* -D / -U の上限 */
#define MAGIC "!<arch>\n"
#define MAGLEN 8
#define HDRLEN 60

/* 大きな作業領域は大域に置く (docs/dev-notes.md 4 章) */
char buf[8192];
char path[512];
char name[512];
char hdr[HDRLEN + 1];

char *ins[NIN];                 /* 入力 (綴りのまま) */
int nin;
char *incs[NINC];               /* -I */
int ninc;
char *defs[NDEF];               /* -D / -U を与えられた順に。頭に D か U */
int ndef;
char *objs[NIN * 8];            /* リンクへ回す .o の経路 */
int nobj;

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

static long fsize(char *p) {
  int s;
  long n;
  s = open(p, O_RDONLY);
  if (s < 0) return -1;
  n = lseek(s, 0, 2);
  close(s);
  return n;
}

/* 綴りが suf で終わるか */
static int endswith(char *s, char *suf) {
  int n;
  int m;
  n = (int)strlen(s);
  m = (int)strlen(suf);
  return n >= m && strcmp(s + n - m, suf) == 0;
}

/* 束ねに 1 つ足す */
static void member(int fd, char *nm, char *from) {
  long sz;
  char h[600];
  sz = fsize(from);
  if (sz < 0) die("cannot open", from);
  sprintf(h, "@%s %ld\n", nm, sz);
  write(fd, h, (int)strlen(h));
  if (copyinto(fd, from) != sz) die("short read", from);
}

/* dir の下のふつうのファイルを束ねへ入れる。pre が空でなければ
 * 名乗る名前の前に付ける (sys/ の階層のため) */
static void adddir(int fd, char *dir, char *pre) {
  DIR *d;
  struct dirent *p;
  d = opendir(dir);
  if (d == 0) return;
  while ((p = readdir(d)) != 0) {
    if (p->d_type == DT_DIR) continue;
    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, p->d_name);
    strcpy(name, pre);
    strcat(name, p->d_name);
    member(fd, name, path);
  }
  closedir(d);
}

/* /include とその下の sys/，および -I で与えられた階層 */
static void addheaders(int fd) {
  int i;
  adddir(fd, INC, "");
  strcpy(path, INC);
  strcat(path, "/sys");
  adddir(fd, path, "sys/");
  for (i = 0; i < ninc; i = i + 1) adddir(fd, incs[i], "");
}

/* -D / -U を並べた前置きを書き，そのあとに src の中身を続ける。
 * **翻訳単位そのものを作り直している**ので，束ねの員としては
 * この一時ファイルを渡す */
static char *makeunit(char *src) {
  int fd;
  int i;
  char line[600];
  fd = open("_cc0.c", O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", "_cc0.c");
  for (i = 0; i < ndef; i = i + 1) {
    char *a;
    a = defs[i] + 1;
    if (defs[i][0] == 'U') {
      sprintf(line, "#undef %s\n", a);
    } else {
      char *eq;
      eq = strchr(a, '=');
      if (eq) {
        *eq = 0;
        sprintf(line, "#define %s %s\n", a, eq + 1);
        *eq = '=';
      } else {
        sprintf(line, "#define %s 1\n", a);
      }
    }
    write(fd, line, (int)strlen(line));
  }
  if (strcmp(src, "-") == 0) {
    int n;
    for (;;) {
      n = read(0, buf, sizeof buf);
      if (n <= 0) break;
      write(fd, buf, n);
    }
  } else {
    if (copyinto(fd, src) < 0) die("cannot open", src);
  }
  close(fd);
  return "_cc0.c";
}

/* .c を 1 本翻訳して out へ .o を置く */
static void compile1(char *src, char *out) {
  int fd;
  int st;
  char *av[4];
  char *unit;
  char *nm;

  unit = makeunit(src);
  /* 名乗る名前は元の綴り。誤りの表示が元のファイル名になる */
  nm = strcmp(src, "-") == 0 ? "stdin.c" : src;

  fd = open(TB, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TB);
  write(fd, "#!stone-bundle\n", 15);
  addheaders(fd);
  member(fd, nm, unit);
  write(fd, "\004", 1);
  close(fd);

  av[0] = PP;
  av[1] = 0;
  st = spawn(PP, av, TB, TI);
  if (st != 0) { fputs("cc: preprocess failed\n", stderr); exit(st); }

  av[0] = CC1;
  av[1] = 0;
  st = spawn(CC1, av, TI, out);
  if (st != 0) { fputs("cc: compile failed\n", stderr); exit(st); }
}

/* 書庫を展開する。員を _a<n>.o として置き，objs へ積む */
static void expand(char *arch) {
  int fd;
  int n;
  char m[MAGLEN];
  int seq;
  fd = open(arch, O_RDONLY);
  if (fd < 0) die("cannot open", arch);
  if (read(fd, m, MAGLEN) != MAGLEN || memcmp(m, MAGIC, MAGLEN) != 0)
    die("not an archive", arch);
  seq = 0;
  for (;;) {
    long sz;
    int i;
    int out;
    long left;
    n = read(fd, hdr, HDRLEN);
    if (n <= 0) break;
    if (n != HDRLEN) die("truncated header", arch);
    sz = 0;
    for (i = 0; i < 10; i = i + 1) {
      char c;
      c = hdr[48 + i];
      if (c < '0' || c > '9') break;
      sz = sz * 10 + (c - '0');
    }
    sprintf(path, "_a%d.o", seq);
    seq = seq + 1;
    out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) die("cannot create", path);
    left = sz;
    while (left > 0) {
      int want;
      want = left > (long)sizeof buf ? (int)sizeof buf : (int)left;
      n = read(fd, buf, want);
      if (n <= 0) break;
      write(out, buf, n);
      left = left - n;
    }
    close(out);
    if (sz & 1) read(fd, buf, 1);       /* 偶数境界の詰め */
    objs[nobj] = malloc((int)strlen(path) + 1);
    strcpy(objs[nobj], path);
    nobj = nobj + 1;
  }
  close(fd);
}

int main(int argc, char **argv) {
  int i;
  int fd;
  int conly;
  int st;
  char *out;
  char *av[4];
  DIR *d;
  struct dirent *p;

  conly = 0;
  out = 0;
  nin = 0;
  ninc = 0;
  ndef = 0;
  nobj = 0;

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
    if (strcmp(a, "-I") == 0) {
      i = i + 1;
      if (i >= argc) die("-I needs an argument", 0);
      if (ninc < NINC) incs[ninc++] = argv[i];
      continue;
    }
    if (strncmp(a, "-I", 2) == 0) {
      if (ninc < NINC) incs[ninc++] = a + 2;
      continue;
    }
    /* -D / -U は与えられた順に覚える。頭の 1 文字で種別を持つ */
    if (strcmp(a, "-D") == 0 || strcmp(a, "-U") == 0) {
      char k;
      k = a[1];
      i = i + 1;
      if (i >= argc) die("-D / -U needs an argument", 0);
      if (ndef < NDEF) {
        defs[ndef] = malloc((int)strlen(argv[i]) + 2);
        defs[ndef][0] = k;
        strcpy(defs[ndef] + 1, argv[i]);
        ndef = ndef + 1;
      }
      continue;
    }
    if (strncmp(a, "-D", 2) == 0 || strncmp(a, "-U", 2) == 0) {
      if (ndef < NDEF) {
        defs[ndef] = malloc((int)strlen(a));
        defs[ndef][0] = a[1];
        strcpy(defs[ndef] + 1, a + 2);
        ndef = ndef + 1;
      }
      continue;
    }
    if (strcmp(a, "-x") == 0) { i = i + 1; continue; }
    if (strncmp(a, "-x", 2) == 0) continue;
    /* 警告の選択肢は黙って受ける。**受けたことを言ってはいけない**
     * (configure は警告文に出てこない選択肢を「使える」と読む。3.3) */
    if (a[0] == '-' && a[1] != 0) continue;
    if (nin < NIN) ins[nin++] = a;       /* "-" もここに来る */
  }
  if (nin == 0) die("no input file", 0);

  /* -c のときは各 .c を .o にして終わる。-o は入力が 1 つのときだけ効く
   * (本物と同じ。複数入力に -o を付ける形は本物も拒む) */
  if (conly) {
    if (out && nin > 1) die("-o with -c allows only one input", 0);
    for (i = 0; i < nin; i = i + 1) {
      char *o;
      if (!endswith(ins[i], ".c") && strcmp(ins[i], "-") != 0)
        die("-c takes .c files", ins[i]);
      if (out) {
        o = out;
      } else {
        /* a.c -> a.o */
        strcpy(name, ins[i]);
        strcpy(name + strlen(name) - 1, "o");
        o = name;
      }
      compile1(ins[i], o);
    }
    return 0;
  }

  /* リンクする。入力を種別で振り分ける */
  for (i = 0; i < nin; i = i + 1) {
    if (endswith(ins[i], ".a")) {
      expand(ins[i]);
    } else if (endswith(ins[i], ".o")) {
      objs[nobj] = ins[i];
      nobj = nobj + 1;
    } else {
      /* .c か "-"。翻訳して .o を積む */
      sprintf(path, "_t%d.o", i);
      compile1(ins[i], path);
      objs[nobj] = malloc((int)strlen(path) + 1);
      strcpy(objs[nobj], path);
      nobj = nobj + 1;
    }
  }
  if (out == 0) out = "a.out";

  /* 'E' + .o の連結 + '\0'。**翻訳単位を先頭に置く**
   * (ld16 は最初の目的ファイルから入口を取る) */
  fd = open(TL, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TL);
  write(fd, "E", 1);
  for (i = 0; i < nobj; i = i + 1)
    if (copyinto(fd, objs[i]) < 0) die("cannot open", objs[i]);
  /* /lib は 1 揃いだけ置くこと (7.2 / 第 1 部 3.2) */
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
