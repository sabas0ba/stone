/* sed3.c --- ストリームエディタの第 3 世代 (docs/stage017-gcc.md 5.7)
 *
 * `sed2` との差は 2 つ。
 *
 *   1. **機構を `re2` に替えた。** 組の中への後戻り・選択 (`\|`)・
 *      `\{n,m\}`・`[[:alpha:]]` が使えるようになり，台本の中で組を
 *      2 度使ったときの番号のずれが直る (5.7)。
 *   2. **照合の手数が上限に当たったら止める。** `re2` は上限に当たると
 *      答が信用できない状態で戻るので，黙って進まずに診断を出す。
 *
 * 受ける形は増えたが，`sed2` が受けていた形の答は変えていない ——
 * `tools/diffsed.sh` がそれを測る。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "re2.h"

#define MAXLINE 8192
#define MAXCMD 256
#define MAXSCRIPT 65536

static char *progname;

static int die(char *msg, char *arg) {
  fprintf(stderr, "sed: %s", msg);
  if (arg) fprintf(stderr, ": %s", arg);
  fprintf(stderr, "\n");
  exit(1);
  return 0;
}

/* 逆斜線の後ろ 1 文字を字へ直す (\n \t \\ など)。置換の側で要る ——
 * 機構の側にも同じものがあるが，あちらは内部の名前である */
static int escchar(int c) {
  if (c == 'n') return '\n';
  if (c == 't') return '\t';
  if (c == 'r') return '\r';
  return c;
}

/* 組む。誤りなら理由を言って止まる —— **機構の側は exit しない**ので，
 * 診断の出し方はここで決める */
static int recompile(char *pat) {
  int h;
  h = re_compile(pat);
  if (h < 0) die(re_errmsg(), pat);
  return h;
}

/* 探す。**手数の上限に当たったら止める** —— 当たった照合は「合わな
 * かった」ではなく「判らなかった」であり，そのまま進むと黙って違う
 * 答を出す */
static int research(int h, char *line, char **mb, char **me) {
  int r;
  r = re_search(h, line, mb, me);
  if (re_overrun()) die("regexp too complex", 0);
  return r;
}

static int researchf(int h, char *ls, char *from, char **mb, char **me) {
  int r;
  r = re_search_from(h, ls, from, mb, me);
  if (re_overrun()) die("regexp too complex", 0);
  return r;
}

/* ================= sed の台本 ================= */

#define A_NONE 0
#define A_LINE 1
#define A_LAST 2
#define A_RE 3

static int a1ty[MAXCMD];
static int a1n[MAXCMD];
static int a1re[MAXCMD];
static int a2ty[MAXCMD];
static int a2n[MAXCMD];
static int a2re[MAXCMD];
static int cneg[MAXCMD];
static int ccmd[MAXCMD];
static int cre[MAXCMD];         /* s の探す形 */
static char *crep[MAXCMD];      /* s の置く形 */
static int cglob[MAXCMD];       /* s の g */
static int cprint[MAXCMD];      /* s の p */
static int cnth[MAXCMD];        /* s の N (既定 1) */
static char *cy1[MAXCMD];       /* y の元 */
static char *cy2[MAXCMD];       /* y の先 */
static char *clab[MAXCMD];      /* b と : の名前 */
static int cjmp[MAXCMD];        /* b の飛び先 (組み立て後に埋める) */
static int cactive[MAXCMD];     /* 範囲の中か */
static int ccnt;

static char scriptbuf[MAXSCRIPT];
static int scriptlen;

static int addscript(char *s) {
  int n;
  n = (int)strlen(s);
  if (scriptlen + n + 2 >= MAXSCRIPT) die("script too long", 0);
  memcpy(scriptbuf + scriptlen, s, n);
  scriptlen = scriptlen + n;
  scriptbuf[scriptlen] = '\n';
  scriptlen = scriptlen + 1;
  scriptbuf[scriptlen] = 0;
  return 0;
}

/* 区切り文字まで写す。逆斜線つきの区切りは字そのものにする */
static char *copyuntil(char *p, int delim, char *out, int outmax) {
  int n;
  n = 0;
  while (*p && *p != delim) {
    if (*p == '\\' && p[1] == delim) {
      if (n + 1 >= outmax) die("pattern too long", 0);
      out[n] = delim;
      n = n + 1;
      p = p + 2;
      continue;
    }
    if (*p == '\\' && p[1]) {
      if (n + 2 >= outmax) die("pattern too long", 0);
      out[n] = '\\';
      out[n + 1] = p[1];
      n = n + 2;
      p = p + 2;
      continue;
    }
    if (n + 1 >= outmax) die("pattern too long", 0);
    out[n] = *p;
    n = n + 1;
    p = p + 1;
  }
  out[n] = 0;
  return p;
}

static char *skipws(char *p) {
  while (*p == ' ' || *p == '\t') p = p + 1;
  return p;
}

/* 番地を 1 つ読む */
static char *readaddr(char *p, int *ty, int *num, int *re) {
  char buf[1024];
  *ty = A_NONE;
  *num = 0;
  *re = -1;
  if (*p >= '0' && *p <= '9') {
    *ty = A_LINE;
    while (*p >= '0' && *p <= '9') {
      *num = *num * 10 + (*p - '0');
      p = p + 1;
    }
    return p;
  }
  if (*p == '$') {
    *ty = A_LAST;
    return p + 1;
  }
  if (*p == '/') {
    p = copyuntil(p + 1, '/', buf, 1024);
    if (*p != '/') die("unterminated address", 0);
    p = p + 1;
    *ty = A_RE;
    *re = recompile(buf);
    return p;
  }
  return p;
}

static char *strsave(char *s) {
  char *d;
  d = (char *)malloc(strlen(s) + 1);
  if (d == 0) die("out of memory", 0);
  strcpy(d, s);
  return d;
}

static int parsescript(void) {
  char *p;
  char buf[4096];
  char rep[4096];
  int i;
  int delim;
  int c;
  p = scriptbuf;
  while (*p) {
    p = skipws(p);
    if (*p == '\n' || *p == ';') { p = p + 1; continue; }
    if (*p == '#') {
      while (*p && *p != '\n') p = p + 1;
      continue;
    }
    if (*p == 0) break;
    if (ccnt >= MAXCMD) die("too many commands", 0);
    i = ccnt;
    a1ty[i] = A_NONE;
    a2ty[i] = A_NONE;
    cneg[i] = 0;
    cglob[i] = 0;
    cprint[i] = 0;
    cnth[i] = 1;
    cjmp[i] = -1;
    cactive[i] = 0;
    clab[i] = 0;
    p = readaddr(p, &a1ty[i], &a1n[i], &a1re[i]);
    if (a1ty[i] != A_NONE && *p == ',') {
      p = readaddr(p + 1, &a2ty[i], &a2n[i], &a2re[i]);
      if (a2ty[i] == A_NONE) die("bad second address", 0);
    }
    p = skipws(p);
    while (*p == '!') { cneg[i] = !cneg[i]; p = p + 1; p = skipws(p); }
    c = *p;
    if (c == 0) break;
    ccmd[i] = c;
    ccnt = ccnt + 1;
    p = p + 1;
    if (c == 's') {
      delim = *p;
      if (delim == 0 || delim == '\n') die("bad s command", 0);
      p = copyuntil(p + 1, delim, buf, 4096);
      if (*p != delim) die("unterminated s", 0);
      p = copyuntil(p + 1, delim, rep, 4096);
      if (*p != delim) die("unterminated s", 0);
      p = p + 1;
      cre[i] = recompile(buf);
      crep[i] = strsave(rep);
      while (*p == 'g' || *p == 'p' || (*p >= '0' && *p <= '9')) {
        if (*p == 'g') cglob[i] = 1;
        else if (*p == 'p') cprint[i] = 1;
        else {
          cnth[i] = 0;
          while (*p >= '0' && *p <= '9') {
            cnth[i] = cnth[i] * 10 + (*p - '0');
            p = p + 1;
          }
          continue;
        }
        p = p + 1;
      }
    } else if (c == 'y') {
      delim = *p;
      p = copyuntil(p + 1, delim, buf, 4096);
      if (*p != delim) die("unterminated y", 0);
      p = copyuntil(p + 1, delim, rep, 4096);
      if (*p != delim) die("unterminated y", 0);
      p = p + 1;
      if (strlen(buf) != strlen(rep)) die("y: lengths differ", 0);
      cy1[i] = strsave(buf);
      cy2[i] = strsave(rep);
    } else if (c == 'b' || c == ':') {
      p = skipws(p);
      {
        int n;
        n = 0;
        while (*p && *p != '\n' && *p != ';' && *p != ' ' && *p != '\t') {
          buf[n] = *p;
          n = n + 1;
          p = p + 1;
        }
        buf[n] = 0;
      }
      clab[i] = strsave(buf);
    } else if (c == 'p' || c == 'd' || c == 'q' || c == '=' || c == 'n'
               || c == 'N' || c == 'D' || c == 'P' || c == '{' || c == '}') {
      /* 引数を取らない */
    } else {
      buf[0] = (char)c;
      buf[1] = 0;
      die("unsupported command", buf);
    }
    /* 命令の後ろは ; か改行 */
    p = skipws(p);
    if (*p == ';' || *p == '\n') p = p + 1;
  }
  /* b の飛び先を解く */
  for (i = 0; i < ccnt; i = i + 1) {
    if (ccmd[i] == 'b') {
      int j;
      if (clab[i][0] == 0) { cjmp[i] = ccnt; continue; }
      cjmp[i] = -1;
      for (j = 0; j < ccnt; j = j + 1) {
        if (ccmd[j] == ':' && strcmp(clab[j], clab[i]) == 0) cjmp[i] = j;
      }
      if (cjmp[i] < 0) die("no such label", clab[i]);
    }
  }
  return 0;
}

/* ================= 実行 ================= */

static char pspace[MAXLINE];    /* 型空間 */
static int lineno;
static int lastline;
static int quiet;
static int exitq;

static FILE *infp;
static char nextbuf[MAXLINE];
static int havenext;

/* 1 行読む。改行は落とす。読めなければ 0 */
static int readline(char *buf) {
  int c;
  int n;
  n = 0;
  c = fgetc(infp);
  if (c == EOF) return 0;
  while (c != EOF && c != '\n') {
    if (n + 1 >= MAXLINE) die("line too long", 0);
    buf[n] = (char)c;
    n = n + 1;
    c = fgetc(infp);
  }
  buf[n] = 0;
  return 1;
}

/* 次の行を先読みして「最後の行か」を決める ($ の番地に要る) */
static int nextline(char *buf) {
  if (havenext) {
    strcpy(buf, nextbuf);
    havenext = 0;
  } else {
    if (!readline(buf)) return 0;
  }
  lineno = lineno + 1;
  if (readline(nextbuf)) havenext = 1;
  else lastline = 1;
  return 1;
}

static int matchaddr(int ty, int n, int re) {
  char *b;
  char *e;
  if (ty == A_LINE) return lineno == n;
  if (ty == A_LAST) return lastline;
  if (ty == A_RE) return research(re, pspace, &b, &e);
  return 0;
}

static int selected(int i) {
  int r;
  if (a1ty[i] == A_NONE) r = 1;
  else if (a2ty[i] == A_NONE) r = matchaddr(a1ty[i], a1n[i], a1re[i]);
  else {
    /* 範囲。開始に合ったら終わりに合うまで続く */
    if (cactive[i]) {
      r = 1;
      if (a2ty[i] == A_LINE) {
        if (lineno >= a2n[i]) cactive[i] = 0;
      } else if (matchaddr(a2ty[i], a2n[i], a2re[i])) {
        cactive[i] = 0;
      }
    } else if (matchaddr(a1ty[i], a1n[i], a1re[i])) {
      r = 1;
      cactive[i] = 1;
      /* 1 行だけの範囲 (終わりが今の行より前) はその場で閉じる */
      if (a2ty[i] == A_LINE && a2n[i] <= lineno) cactive[i] = 0;
    } else {
      r = 0;
    }
  }
  if (cneg[i]) r = !r;
  return r;
}

/* 置く形を展開して out へ書く。& は合った全体，\1〜\9 は組 */
static int expand(char *rep, char *mb, char *me, char *out, int *outn) {
  char *p;
  int n;
  int g;
  char *b;
  char *e;
  n = *outn;
  p = rep;
  while (*p) {
    if (*p == '&') {
      b = mb;
      while (b < me) {
        if (n + 1 >= MAXLINE) die("line too long", 0);
        out[n] = *b;
        n = n + 1;
        b = b + 1;
      }
      p = p + 1;
      continue;
    }
    if (*p == '\\' && p[1] >= '1' && p[1] <= '9') {
      g = p[1] - '0';
      b = re_gs(g);
      e = re_ge(g);
      if (b && e) {
        while (b < e) {
          if (n + 1 >= MAXLINE) die("line too long", 0);
          out[n] = *b;
          n = n + 1;
          b = b + 1;
        }
      }
      p = p + 2;
      continue;
    }
    if (*p == '\\' && p[1]) {
      if (n + 1 >= MAXLINE) die("line too long", 0);
      out[n] = (char)escchar((unsigned char)p[1]);
      n = n + 1;
      p = p + 2;
      continue;
    }
    if (n + 1 >= MAXLINE) die("line too long", 0);
    out[n] = *p;
    n = n + 1;
    p = p + 1;
  }
  *outn = n;
  return 0;
}

static int dosub(int i) {
  char out[MAXLINE];
  char *s;
  char *mb;
  char *me;
  char *lastend;
  int n;
  int hit;
  int done;
  n = 0;
  s = pspace;
  hit = 0;
  done = 0;
  lastend = 0;
  while (1) {
    mb = 0;
    me = 0;
    {
      char *t;
      t = s;
      while (1) {
        if (!researchf(cre[i], pspace, t, &mb, &me)) { mb = 0; break; }
        /* **空に合う形が直前の合致の直後に来たら飛ばす** (POSIX)。
         * "x" に x の 0 回以上を全置換すると、x を置いた後の行末でも
         * 空に合うので、飛ばさないと置換後の字が 2 つ出る */
        if (!(mb == me && mb == lastend)) break;
        if (*mb == 0) { mb = 0; break; }
        t = mb + 1;
      }
    }
    if (mb == 0) break;
    hit = hit + 1;
    /* 合った手前を写す */
    while (s < mb) {
      if (n + 1 >= MAXLINE) die("line too long", 0);
      out[n] = *s;
      n = n + 1;
      s = s + 1;
    }
    if (hit >= cnth[i]) {
      expand(crep[i], mb, me, out, &n);
      done = 1;
    } else {
      while (s < me) {
        if (n + 1 >= MAXLINE) die("line too long", 0);
        out[n] = *s;
        n = n + 1;
        s = s + 1;
      }
    }
    s = me;
    lastend = me;
    if (mb == me) {
      /* 空に合った。1 文字進めないと止まらない */
      if (*s == 0) break;
      if (n + 1 >= MAXLINE) die("line too long", 0);
      out[n] = *s;
      n = n + 1;
      s = s + 1;
    }
    if (done && !cglob[i]) break;
  }
  if (!done) return 0;
  while (*s) {
    if (n + 1 >= MAXLINE) die("line too long", 0);
    out[n] = *s;
    n = n + 1;
    s = s + 1;
  }
  out[n] = 0;
  strcpy(pspace, out);
  return 1;
}

static int doy(int i) {
  char *p;
  char *q;
  int k;
  p = pspace;
  while (*p) {
    q = strchr(cy1[i], *p);
    if (q) {
      k = (int)(q - cy1[i]);
      *p = cy2[i][k];
    }
    p = p + 1;
  }
  return 0;
}

/* 台本を 1 行に当てる。0 = 続ける / 1 = 出力せずに次の行へ / 2 = 終わり */
static int runline(void) {
  int i;
  int r;
  i = 0;
  while (i < ccnt) {
    if (ccmd[i] == ':') { i = i + 1; continue; }
    if (!selected(i)) {
      if (ccmd[i] == '{') {
        /* 選ばれなかった塊は丸ごと飛ばす */
        int depth;
        depth = 1;
        i = i + 1;
        while (i < ccnt && depth > 0) {
          if (ccmd[i] == '{') depth = depth + 1;
          if (ccmd[i] == '}') depth = depth - 1;
          i = i + 1;
        }
        continue;
      }
      i = i + 1;
      continue;
    }
    if (ccmd[i] == '{' || ccmd[i] == '}') { i = i + 1; continue; }
    if (ccmd[i] == 's') {
      r = dosub(i);
      if (r && cprint[i]) printf("%s\n", pspace);
      i = i + 1;
      continue;
    }
    if (ccmd[i] == 'y') { doy(i); i = i + 1; continue; }
    if (ccmd[i] == 'p') { printf("%s\n", pspace); i = i + 1; continue; }
    if (ccmd[i] == 'P') {
      char *nl;
      nl = strchr(pspace, '\n');
      if (nl) {
        *nl = 0;
        printf("%s\n", pspace);
        *nl = '\n';
      } else {
        printf("%s\n", pspace);
      }
      i = i + 1;
      continue;
    }
    if (ccmd[i] == '=') { printf("%d\n", lineno); i = i + 1; continue; }
    if (ccmd[i] == 'd') return 1;
    if (ccmd[i] == 'D') {
      char *nl;
      nl = strchr(pspace, '\n');
      if (nl == 0) return 1;
      strcpy(pspace, nl + 1);
      return 3;                 /* 出力せずに台本の先頭から */
    }
    if (ccmd[i] == 'q') { exitq = 1; return 0; }
    if (ccmd[i] == 'n') {
      char buf[MAXLINE];
      /* **自動出力はここで済ませる。** 次の行が無ければそのまま終わる
       * ので、呼び手の側でもう一度出すと同じ行が 2 度出る */
      if (!quiet) printf("%s\n", pspace);
      if (!nextline(buf)) return 4;
      strcpy(pspace, buf);
      i = i + 1;
      continue;
    }
    if (ccmd[i] == 'N') {
      char buf[MAXLINE];
      if (!nextline(buf)) return 2;
      if ((int)(strlen(pspace) + strlen(buf) + 2) >= MAXLINE)
        die("line too long", 0);
      strcat(pspace, "\n");
      strcat(pspace, buf);
      i = i + 1;
      continue;
    }
    if (ccmd[i] == 'b') {
      i = cjmp[i];
      continue;
    }
    i = i + 1;
  }
  return 0;
}

static int runfile(void) {
  char buf[MAXLINE];
  int r;
  lineno = 0;
  lastline = 0;
  havenext = 0;
  while (nextline(buf)) {
    strcpy(pspace, buf);
    while (1) {
      r = runline();
      if (r != 3) break;
    }
    if (r == 0 && !quiet) printf("%s\n", pspace);
    /* N が入力の終わりに当たった。GNU sed は型空間を出してから終わる */
    if (r == 2) {
      if (!quiet) printf("%s\n", pspace);
      break;
    }
    /* n が入力の終わりに当たった。出力は n の側で済んでいる */
    if (r == 4) break;
    if (exitq) break;
  }
  return 0;
}

int main(int argc, char **argv) {
  int i;
  int havescript;
  char *files[64];
  int nfile;
  progname = argv[0];
  havescript = 0;
  nfile = 0;
  quiet = 0;
  exitq = 0;
  ccnt = 0;
  re_reset();
  scriptlen = 0;
  i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "-n") == 0) { quiet = 1; i = i + 1; continue; }
    if (strcmp(argv[i], "-e") == 0) {
      if (i + 1 >= argc) die("-e needs an argument", 0);
      addscript(argv[i + 1]);
      havescript = 1;
      i = i + 2;
      continue;
    }
    if (strcmp(argv[i], "-f") == 0) {
      FILE *f;
      char buf[MAXLINE];
      if (i + 1 >= argc) die("-f needs an argument", 0);
      f = fopen(argv[i + 1], "r");
      if (f == 0) die("cannot open", argv[i + 1]);
      while (fgets(buf, MAXLINE, f)) {
        int n;
        n = (int)strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = 0;
        addscript(buf);
      }
      fclose(f);
      havescript = 1;
      i = i + 2;
      continue;
    }
    if (strcmp(argv[i], "--") == 0) { i = i + 1; break; }
    if (argv[i][0] == '-' && argv[i][1] != 0) die("unknown option", argv[i]);
    break;
  }
  if (!havescript) {
    if (i >= argc) die("no script", 0);
    addscript(argv[i]);
    i = i + 1;
  }
  while (i < argc) {
    if (nfile >= 64) die("too many files", 0);
    files[nfile] = argv[i];
    nfile = nfile + 1;
    i = i + 1;
  }
  parsescript();
  if (nfile == 0) {
    infp = stdin;
    runfile();
  } else {
    int k;
    for (k = 0; k < nfile; k = k + 1) {
      infp = fopen(files[k], "r");
      if (infp == 0) die("cannot open", files[k]);
      runfile();
      fclose(infp);
      if (exitq) break;
    }
  }
  return 0;
}
