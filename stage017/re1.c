/* re1.c --- 正規表現機構の第 1 世代 (docs/stage017-gcc.md 5.6)
 *
 * `sed1.c` が持っていた機構をそのまま切り出したものである。切り出した
 * のは **grep も同じものを使うため**で，写しを 2 つ持つと必ず片方だけ
 * 直す誤りが出る。
 *
 * 切り出しにあたって変えたのは 1 つだけ ——
 * **組む側の誤りで exit しない**。道具の側が診断の出し方を決められる
 * ように，-1 を返して理由を re_errmsg() に置く。照合の側は元のままで
 * ある (誤りを返す道が無い)。
 *
 * 中身の設計は re1.h の註を見よ。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "re1.h"

#define MAXNODE 1024
#define MAXSET 64

static char errbuf[128];
static int failed;

char *re_errmsg(void) { return errbuf; }

/* 組む途中の誤り。**exit しない** —— 呼ぶ側が決める */
static int reerr(char *msg) {
  int n;
  n = (int)strlen(msg);
  if (n > 120) n = 120;
  memcpy(errbuf, msg, n);
  errbuf[n] = 0;
  failed = 1;
  return -1;
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
  if (ncnt >= MAXNODE) { reerr("regexp too big"); return 0; }
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
  if (*p != ']') { reerr("unterminated ["); return p; }
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
    if (setcnt >= MAXSET) { reerr("too many [ ]"); return 0; }
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
    if (ngrp >= 9) { reerr("too many groups"); return 0; }
    ngrp = ngrp + 1;
    g = ngrp;
    nd = newnode(N_GRP);
    nch[nd] = g;
    p = p + 2;
    nsub[nd] = reseq(&p, depth + 1);
    if (!(p[0] == '\\' && p[1] == ')')) { reerr("unmatched \\("); *pp = p; return nd; }
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
  if (*p == '\\' && p[1] == '{') { reerr("\\{ } is not supported"); *pp = p; return 0; }
  if (*p == '\\' && p[1] == '|') { reerr("\\| is not supported"); *pp = p; return 0; }
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
int re_compile(char *pat) {
  char *p;
  int h;
  failed = 0;
  errbuf[0] = 0;
  p = pat;
  h = reseq(&p, 0);
  if (failed) return -1;
  if (*p) return reerr("trailing garbage in regexp");
  return h;
}

int re_reset(void) {
  ncnt = 0;
  setcnt = 0;
  ngrp = 0;
  return 0;
}

char *re_gs(int n) { return gs[n]; }
char *re_ge(int n) { return ge[n]; }

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
int re_search(int head, char *line, char **mb, char **me) {
  return re_search_from(head, line, line, mb, me);
}

int re_search_from(int head, char *lstart, char *from, char **mb, char **me) {
  char *s;
  char *r;
  int i;
  linestart = lstart;
  s = from;
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

