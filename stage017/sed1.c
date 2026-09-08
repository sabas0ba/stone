/* sed1.c --- ストリームエディタの第 1 世代 (docs/stage017-gcc.md 5.5)
 *
 * **我々の OS には正規表現が 1 つも無い。** `sh2` の `grep` は固定
 * 文字列としてしか探さず (stage016/sh2.c の註)，`sed` は存在しない。
 *
 * これは 4.2 (GCC の `configure` が我々の OS で回るか) の正面の壁で
 * ある。autoconf が作る `configure` は，変数の展開・置換・`config.status`
 * の生成まで**ほぼすべてを sed でやる**。tcc の `configure` が通った
 * のは手書きで sed をほとんど使わないからで，そこは測れていなかった。
 *
 * 本ファイルは 2 つを持つ。
 *
 *   1. POSIX BRE の部分集合を解する正規表現機構
 *   2. その上に立つ sed
 *
 * **物差しはホストの sed である** (tools/diffsed.sh)。我々が期待値を
 * 書くのではなく，同じ台本と同じ入力をホストの sed にも食わせて出力を
 * 突き合わせる (5.2 と同じ筋)。
 *
 * ---- 受ける形 ----
 *
 *   正規表現   ^ $ . * [...] [^...] [a-z] \( \) \1〜\9 \+ \? \. \\ \n \t
 *   命令       s/re/rep/[gp数] y/../../ p d q = n N D P b : ! { }
 *   番地       行番号 / $ / /re/ / addr1,addr2 / 番地の後の !
 *   引数       -n -e 台本 -f ファイル -- / 最初の非オプションが台本
 *   置換       & \1〜\9 \& \\ \n
 *
 * ---- 受けない形 ----
 *
 * **無いものは無いと言う。** 知らない命令・知らない綴りは，黙って
 * 読み飛ばさずに診断を出して終了コード 1 で止まる。
 *
 *   \{n,m\}    区間量指定子。autoconf の台本には出てこない
 *   \|         GNU の選択。POSIX BRE には無い
 *   a i c r w  行の追加・読み書き。configure は使わない
 *   最左最長    我々は**最左・貪欲**である (下の註)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 8192
#define MAXNODE 1024
#define MAXSET 64
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

/* ================= 正規表現 (POSIX BRE の部分集合) =================
 *
 * 原文を辿る形ではなく，節の配列へ組んでから照合する。`\(...\)*` の
 * ように**塊に量指定子が付く**形があるので，塊の範囲を節として持って
 * いないと後戻りができない。
 *
 * 照合は後戻り (backtracking) である。**最左最長ではなく最左・貪欲**
 * で，量指定子は長いほうから順に試す。POSIX は最長一致を要求するので，
 * `\(a\|ab\)` のような選択がある形では違いが出るが，選択は受けない
 * ので，我々が受ける形の範囲では**同じ結果になる**。
 */
#define N_CHAR 1
#define N_ANY 2
#define N_SET 3
#define N_BOL 4
#define N_EOL 5
#define N_GRP 6
#define N_BREF 7

static int nkind[MAXNODE];
static int nch[MAXNODE];        /* N_CHAR の文字 / N_BREF の番号 / N_GRP の番号 */
static int nset[MAXNODE];       /* N_SET の集合表の番号 */
static int nsub[MAXNODE];       /* N_GRP の中身の先頭節 (-1 = 空) */
static int nrep[MAXNODE];       /* 0 = 1 回, 1 = *, 2 = \+, 3 = \? */
static int nnext[MAXNODE];      /* 並びの次 (-1 = 終わり) */
static int ncnt;

static char cset[MAXSET][256];
static int setcnt;

static int ngrp;                /* 組んだ組の数 */
static char *gs[10];            /* 組の開始 */
static char *ge[10];            /* 組の終わり */

static int newnode(int kind) {
  int i;
  if (ncnt >= MAXNODE) die("regexp too big", 0);
  i = ncnt;
  ncnt = ncnt + 1;
  nkind[i] = kind;
  nch[i] = 0;
  nset[i] = -1;
  nsub[i] = -1;
  nrep[i] = 0;
  nnext[i] = -1;
  return i;
}

static int reseq(char **pp, int depth);

/* 括弧の中の 1 文字を読む。範囲 (a-z) と否定 (^) を受ける */
static char *readset(char *p, int si) {
  int neg;
  int i;
  int c;
  int prev;
  neg = 0;
  for (i = 0; i < 256; i = i + 1) cset[si][i] = 0;
  if (*p == '^') { neg = 1; p = p + 1; }
  prev = -1;
  /* 先頭の ] は字そのものである (POSIX) */
  if (*p == ']') { cset[si][(unsigned char)']'] = 1; prev = ']'; p = p + 1; }
  while (*p && *p != ']') {
    if (*p == '-' && prev >= 0 && p[1] && p[1] != ']') {
      c = (unsigned char)p[1];
      for (i = prev; i <= c; i = i + 1) cset[si][i] = 1;
      prev = -1;
      p = p + 2;
      continue;
    }
    c = (unsigned char)*p;
    cset[si][c] = 1;
    prev = c;
    p = p + 1;
  }
  if (*p != ']') die("unterminated [", 0);
  p = p + 1;
  if (neg) {
    for (i = 0; i < 256; i = i + 1) cset[si][i] = !cset[si][i];
    cset[si][0] = 0;
  }
  return p;
}

/* 逆斜線の後ろ 1 文字を字へ直す (\n \t \\ など) */
static int escchar(int c) {
  if (c == 'n') return '\n';
  if (c == 't') return '\t';
  if (c == 'r') return '\r';
  return c;
}

/* 1 つの原子を作る。読み進めた位置を *pp へ返す */
static int reatom(char **pp, int depth) {
  char *p;
  int nd;
  int g;
  p = *pp;
  if (*p == '[') {
    if (setcnt >= MAXSET) die("too many [ ]", 0);
    nd = newnode(N_SET);
    nset[nd] = setcnt;
    p = readset(p + 1, setcnt);
    setcnt = setcnt + 1;
    *pp = p;
    return nd;
  }
  if (*p == '.') {
    nd = newnode(N_ANY);
    *pp = p + 1;
    return nd;
  }
  if (*p == '\\' && p[1] == '(') {
    if (ngrp >= 9) die("too many groups", 0);
    ngrp = ngrp + 1;
    g = ngrp;
    nd = newnode(N_GRP);
    nch[nd] = g;
    p = p + 2;
    nsub[nd] = reseq(&p, depth + 1);
    if (!(p[0] == '\\' && p[1] == ')')) die("unmatched \\(", 0);
    p = p + 2;
    *pp = p;
    return nd;
  }
  if (*p == '\\' && p[1] >= '1' && p[1] <= '9') {
    nd = newnode(N_BREF);
    nch[nd] = p[1] - '0';
    *pp = p + 2;
    return nd;
  }
  if (*p == '\\' && p[1] == '{') die("\\{ } is not supported", 0);
  if (*p == '\\' && p[1] == '|') die("\\| is not supported", 0);
  if (*p == '\\' && p[1]) {
    nd = newnode(N_CHAR);
    nch[nd] = escchar((unsigned char)p[1]);
    *pp = p + 2;
    return nd;
  }
  nd = newnode(N_CHAR);
  nch[nd] = (unsigned char)*p;
  *pp = p + 1;
  return nd;
}

/* 並びを作る。深さ 0 なら末尾まで，深さ 1 以上なら \) で止まる */
static int reseq(char **pp, int depth) {
  char *p;
  int head;
  int tail;
  int nd;
  int first;
  p = *pp;
  head = -1;
  tail = -1;
  first = 1;
  while (*p) {
    if (depth > 0 && p[0] == '\\' && p[1] == ')') break;
    if (*p == '^' && first) {
      nd = newnode(N_BOL);
      p = p + 1;
    } else if (*p == '$' && (p[1] == 0 || (depth > 0 && p[1] == '\\' && p[2] == ')'))) {
      nd = newnode(N_EOL);
      p = p + 1;
    } else {
      nd = reatom(&p, depth);
      /* 量指定子。* は素で，\+ と \? は逆斜線つき (POSIX BRE) */
      if (*p == '*') { nrep[nd] = 1; p = p + 1; }
      else if (p[0] == '\\' && p[1] == '+') { nrep[nd] = 2; p = p + 2; }
      else if (p[0] == '\\' && p[1] == '?') { nrep[nd] = 3; p = p + 2; }
    }
    if (head < 0) head = nd;
    else nnext[tail] = nd;
    tail = nd;
    first = 0;
  }
  *pp = p;
  return head;
}

/* 組んだ正規表現の先頭節。台本の命令ごとに持つ */
static int recompile(char *pat) {
  char *p;
  int h;
  p = pat;
  h = reseq(&p, 0);
  if (*p) die("trailing garbage in regexp", pat);
  return h;
}

static char *linestart;         /* ^ の判定に使う行頭 */

static char *mnode(int ni, char *s);

/* 節 1 個ぶんの照合 (量指定子は見ない)。合えば到達点，合わなければ 0 */
static char *mone(int ni, char *s) {
  int k;
  int n;
  char *b;
  char *e;
  k = nkind[ni];
  if (k == N_CHAR) {
    if (*s && (unsigned char)*s == nch[ni]) return s + 1;
    return 0;
  }
  if (k == N_ANY) {
    if (*s) return s + 1;
    return 0;
  }
  if (k == N_SET) {
    if (*s && cset[nset[ni]][(unsigned char)*s]) return s + 1;
    return 0;
  }
  if (k == N_BOL) {
    if (s == linestart) return s;
    return 0;
  }
  if (k == N_EOL) {
    if (*s == 0) return s;
    return 0;
  }
  if (k == N_BREF) {
    n = nch[ni];
    b = gs[n];
    e = ge[n];
    if (b == 0 || e == 0) return 0;
    while (b < e) {
      if (*s == 0 || *s != *b) return 0;
      s = s + 1;
      b = b + 1;
    }
    return s;
  }
  return 0;                     /* N_GRP は mnode が扱う */
}

/* 組を 1 回照合し，続きへ進む。組の中は並びなので mnode を使う。
 * **組の終わりで続きへ抜ける**ために，中身の末尾に続きを繋ぐのでは
 * なく，中身だけを照合して到達点を数える形にする */
static char *mgrp1(int ni, char *s) {
  int sub;
  sub = nsub[ni];
  if (sub < 0) return s;        /* \(\) は空に合う */
  return mnode(sub, s);
}

/* 組の繰返しで控える到達点の数。1 文字の節は s + k で数えられるので
 * 表を持たない —— **持つと 1 段ごとに行の長さぶんの積みを食う** */
#define MAXGREP 256

static char *mnode(int ni, char *s) {
  char *r;
  char *ends[MAXGREP + 1];
  int n;
  int g;
  char *sb[10];
  char *se[10];
  int i;
  if (ni < 0) return s;
  if (nrep[ni] == 0) {
    if (nkind[ni] == N_GRP) {
      g = nch[ni];
      sb[0] = gs[g];
      se[0] = ge[g];
      /* 組の中身を照合し，合った範囲を控えてから続きへ */
      ends[0] = mgrp1(ni, s);
      if (ends[0] == 0) return 0;
      gs[g] = s;
      ge[g] = ends[0];
      r = mnode(nnext[ni], ends[0]);
      if (r) return r;
      gs[g] = sb[0];
      ge[g] = se[0];
      return 0;
    }
    r = mone(ni, s);
    if (r == 0) return 0;
    return mnode(nnext[ni], r);
  }
  /* 量指定子つき。**長いほうから試す** */
  for (i = 0; i < 10; i = i + 1) { sb[i] = gs[i]; se[i] = ge[i]; }
  if (nkind[ni] != N_GRP) {
    /* 1 文字の節。到達点は s + k なので表が要らない */
    n = 0;
    while (1) {
      if (nrep[ni] == 3 && n >= 1) break;   /* 逆斜線つきの ? は高々 1 回 */
      if (mone(ni, s + n) == 0) break;
      n = n + 1;
    }
    while (n >= 0) {
      if (nrep[ni] == 2 && n == 0) break;   /* 逆斜線つきの + は 1 回以上 */
      r = mnode(nnext[ni], s + n);
      if (r) return r;
      n = n - 1;
    }
    return 0;
  }
  n = 0;
  ends[0] = s;
  while (n < MAXGREP) {
    r = mgrp1(ni, ends[n]);
    if (r == 0) break;
    if (r == ends[n]) break;    /* 空に合う形。無限に伸ばさない */
    n = n + 1;
    ends[n] = r;
    if (nrep[ni] == 3) break;   /* 逆斜線つきの ? は高々 1 回 */
  }
  while (n >= 0) {
    if (nrep[ni] == 2 && n == 0) break;   /* 逆斜線つきの + は 1 回以上 */
    if (n > 0) {
      /* 最後の 1 回の範囲を組として控える (POSIX の規則) */
      g = nch[ni];
      gs[g] = ends[n - 1];
      ge[g] = ends[n];
    }
    r = mnode(nnext[ni], ends[n]);
    if (r) return r;
    n = n - 1;
  }
  for (i = 0; i < 10; i = i + 1) { gs[i] = sb[i]; ge[i] = se[i]; }
  return 0;
}

/* 行のどこかに合うか。合えば 1 を返し，*mb / *me に範囲を入れる */
static int research(int head, char *line, char **mb, char **me) {
  char *s;
  char *r;
  int i;
  linestart = line;
  s = line;
  while (1) {
    for (i = 0; i < 10; i = i + 1) { gs[i] = 0; ge[i] = 0; }
    r = mnode(head, s);
    if (r) {
      *mb = s;
      *me = r;
      return 1;
    }
    if (*s == 0) return 0;
    s = s + 1;
  }
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
      b = gs[g];
      e = ge[g];
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
  linestart = pspace;
  while (1) {
    int k;
    for (k = 0; k < 10; k = k + 1) { gs[k] = 0; ge[k] = 0; }
    mb = 0;
    me = 0;
    {
      char *t;
      char *r;
      t = s;
      while (1) {
        int j;
        for (j = 0; j < 10; j = j + 1) { gs[j] = 0; ge[j] = 0; }
        r = mnode(cre[i], t);
        /* **空に合う形が直前の合致の直後に来たら飛ばす** (POSIX)。
         * "x" に x の 0 回以上を全置換すると、x を置いた後の行末でも
         * 空に合うので、飛ばさないと置換後の字が 2 つ出る */
        if (r && !(r == t && t == lastend)) { mb = t; me = r; break; }
        if (*t == 0) break;
        t = t + 1;
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
  ncnt = 0;
  setcnt = 0;
  ngrp = 0;
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
