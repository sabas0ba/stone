/* re2.c --- 正規表現機構の第 2 世代 (docs/stage017-gcc.md 5.7)
 *
 * 設計の意図は re2.h の註にある。ここでは中身の作りだけを書く。
 *
 * ## 節の木ではなく命令列にした
 *
 * re1 は正規表現を節の木に組み，木を辿って照合していた。この形だと
 * **組 (`\( \)`) の中へ後戻りできない** —— 組を 1 回照合して到達点を
 * 1 つ返す作りなので，続きが外れたときに「組の取り方を変えてやり直す」
 * 道が無い。`\(a*\)ab` が "aab" に合わないのはこれである。
 *
 * 命令列にすると，後戻りは**続きを引数に取った再帰**そのものになる。
 * SPLIT が 2 つの道を順に試し，どちらも最後の MATCH まで辿る。組の中で
 * あろうが外であろうが同じ 1 つの仕組みで済む。選択 (`|`) も SPLIT が
 * そのまま担う。
 *
 * ## 手数と段数
 *
 * 素直に書くと `a*` のような形で**入力の長さぶんの再帰**が積む。行が
 * 8 KiB あると段数もそれに比例して危ない。そこで **1 文字の節の繰返し
 * だけは I_REP という 1 命令に畳んだ** —— 回数を数える繰返しは再帰
 * しない。段数が入力に比例するのは組の繰返し (`\(ab\)*`) だけになる。
 *
 * 手数には上限を置く。上限に当たった照合は答が信用できないので，
 * re_overrun() で外へ伝える。**黙って「合わなかった」と言わない。**
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "re2.h"

#define MAXPROG 4096
#define MAXSET 96
#define MAXMARK 64
#define MAXBLOCK 512
#define MAXOPT 64
#define STEPLIMIT 2000000

#define I_CHAR 1
#define I_ANY 2
#define I_SET 3
#define I_BOL 4
#define I_EOL 5
#define I_SPLIT 6
#define I_JMP 7
#define I_SAVE 8
#define I_BREF 9
#define I_MARK 10
#define I_REP 11
#define I_MATCH 12

static int iop[MAXPROG];
static int ia[MAXPROG];
static int ib[MAXPROG];
static int pc;

static char cset[MAXSET][256];
static int setcnt;

static char plong[MAXPROG];     /* 先頭命令ごとに「全探索が要るか」 */

static int nmark;
static int ngrp;
static int ere;
static int havealt;

static char errbuf[128];
static int failed;
static int overrun;

char *re_errmsg(void) { return errbuf; }
int re_overrun(void) { return overrun; }

/* 組む途中の誤り。**exit しない** —— 呼ぶ側が診断の出し方を決める */
static int reerr(char *msg) {
  int n;
  if (!failed) {
    n = (int)strlen(msg);
    if (n > 120) n = 120;
    memcpy(errbuf, msg, n);
    errbuf[n] = 0;
  }
  failed = 1;
  return -1;
}

static int emit(int op, int a, int b) {
  int i;
  if (pc >= MAXPROG) { reerr("regexp too big"); return 0; }
  i = pc;
  pc = pc + 1;
  iop[i] = op;
  ia[i] = a;
  ib[i] = b;
  return i;
}

/* at の位置に n 個の隙間を空ける。**飛び先はすべて絶対番地**なので，
 * at 以上を指しているものを n だけずらす。量指定子は「後ろから前へ
 * かぶせる」形なので，この操作が要る */
static int ins(int at, int n) {
  int i;
  if (pc + n > MAXPROG) { reerr("regexp too big"); return -1; }
  i = pc - 1;
  while (i >= at) {
    iop[i + n] = iop[i];
    ia[i + n] = ia[i];
    ib[i + n] = ib[i];
    i = i - 1;
  }
  for (i = 0; i < n; i = i + 1) {
    iop[at + i] = 0;
    ia[at + i] = 0;
    ib[at + i] = 0;
  }
  pc = pc + n;
  for (i = 0; i < pc; i = i + 1) {
    if (i >= at && i < at + n) continue;
    if (iop[i] == I_SPLIT) {
      if (ia[i] >= at) ia[i] = ia[i] + n;
      if (ib[i] >= at) ib[i] = ib[i] + n;
    } else if (iop[i] == I_JMP) {
      if (ia[i] >= at) ia[i] = ia[i] + n;
    }
  }
  return 0;
}

/* ---- 塊の写し (`{n,m}` が使う) ---- */
static int blkop[MAXBLOCK];
static int blka[MAXBLOCK];
static int blkb[MAXBLOCK];
static int blen;
static int bbase;

static int takeblk(int a) {
  int i;
  blen = pc - a;
  if (blen > MAXBLOCK) { reerr("regexp too big"); return -1; }
  for (i = 0; i < blen; i = i + 1) {
    blkop[i] = iop[a + i];
    blka[i] = ia[a + i];
    blkb[i] = ib[a + i];
  }
  bbase = a;
  pc = a;
  return 0;
}

static int putblk(void) {
  int i;
  int base;
  int op;
  base = pc;
  if (pc + blen > MAXPROG) { reerr("regexp too big"); return -1; }
  for (i = 0; i < blen; i = i + 1) {
    op = blkop[i];
    iop[base + i] = op;
    ia[base + i] = blka[i];
    ib[base + i] = blkb[i];
    if (op == I_SPLIT) {
      ia[base + i] = blka[i] - bbase + base;
      ib[base + i] = blkb[i] - bbase + base;
    } else if (op == I_JMP) {
      ia[base + i] = blka[i] - bbase + base;
    } else if (op == I_MARK) {
      /* 写しごとに別の印を配る。**同じ印を使い回すと，外側の写しが
       * 立てた印で内側の写しが止まる** */
      if (nmark >= MAXMARK) { reerr("regexp too big"); return -1; }
      ia[base + i] = nmark;
      nmark = nmark + 1;
    }
  }
  pc = base + blen;
  return base;
}

/* ================= 括弧の中 ================= */

static int isal(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int isdi(int c) { return c >= '0' && c <= '9'; }
static int issp(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
static int ishx(int c) {
  return isdi(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* [:alpha:] の類。**ホストの ctype に頼らない** —— 地域設定で答が
 * 変わると，物差しと突き合わせたときに我々の誤りと区別できなくなる */
static int addclass(int si, char *nm) {
  int i;
  int c;
  for (i = 1; i < 256; i = i + 1) {
    c = 0;
    if (strcmp(nm, "alpha") == 0) c = isal(i);
    else if (strcmp(nm, "digit") == 0) c = isdi(i);
    else if (strcmp(nm, "alnum") == 0) c = isal(i) || isdi(i);
    else if (strcmp(nm, "upper") == 0) c = (i >= 'A' && i <= 'Z');
    else if (strcmp(nm, "lower") == 0) c = (i >= 'a' && i <= 'z');
    else if (strcmp(nm, "space") == 0) c = issp(i);
    else if (strcmp(nm, "blank") == 0) c = (i == ' ' || i == '\t');
    else if (strcmp(nm, "xdigit") == 0) c = ishx(i);
    else if (strcmp(nm, "cntrl") == 0) c = (i < 32 || i == 127);
    else if (strcmp(nm, "print") == 0) c = (i >= 32 && i < 127);
    else if (strcmp(nm, "graph") == 0) c = (i > 32 && i < 127);
    else if (strcmp(nm, "punct") == 0)
      c = (i > 32 && i < 127) && !isal(i) && !isdi(i);
    else return reerr("unknown [: :] class");
    if (c) cset[si][i] = 1;
  }
  return 0;
}

/* 括弧の中を読む。範囲 (a-z)・否定 (^)・字種 ([:alpha:]) を受ける。
 * **逆斜線は字そのもの** (POSIX)。GNU の sed は括弧の中でも \n を
 * 読むが，そちらへ寄せると POSIX の台本で黙って違う答になる */
static char *readset(char *p, int si) {
  int neg;
  int i;
  int c;
  int prev;
  int nl;
  char nm[16];
  neg = 0;
  for (i = 0; i < 256; i = i + 1) cset[si][i] = 0;
  if (*p == '^') { neg = 1; p = p + 1; }
  prev = -1;
  if (*p == ']') { cset[si][(unsigned char)']'] = 1; prev = ']'; p = p + 1; }
  while (*p && *p != ']') {
    if (p[0] == '[' && p[1] == ':') {
      p = p + 2;
      nl = 0;
      while (*p && *p != ':' && nl < 15) { nm[nl] = *p; nl = nl + 1; p = p + 1; }
      nm[nl] = 0;
      if (!(p[0] == ':' && p[1] == ']')) { reerr("unterminated [: :]"); return p; }
      p = p + 2;
      addclass(si, nm);
      prev = -1;
      continue;
    }
    if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
      reerr("[. .] and [= =] are not supported");
      return p;
    }
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

/* ================= 組む ================= */

static char *cp;

static int escchar(int c) {
  if (c == 'n') return '\n';
  if (c == 't') return '\t';
  if (c == 'r') return '\r';
  return c;
}

/* BRE と ERE の違いは「演算子に逆斜線が要るか」だけである。
 * 判定を 1 か所に集めて，読み手が両方を追わずに済むようにする */
static int alt_here(void) {
  if (ere) return cp[0] == '|';
  return cp[0] == '\\' && cp[1] == '|';
}
static int rp_here(void) {
  if (ere) return cp[0] == ')';
  return cp[0] == '\\' && cp[1] == ')';
}
static int lp_here(void) {
  if (ere) return cp[0] == '(';
  return cp[0] == '\\' && cp[1] == '(';
}
static int skipop(void) {
  if (ere) cp = cp + 1;
  else cp = cp + 2;
  return 0;
}
static int eol_here(void) {
  char *sv;
  int r;
  if (cp[0] != '$') return 0;
  if (ere) return 1;
  sv = cp;
  cp = cp + 1;
  r = (cp[0] == 0) || rp_here() || alt_here();
  cp = sv;
  return r;
}

static int calt(void);

/* 原子を 1 つ組む。first は「並びの先頭か」—— BRE の ^ と * の扱いに要る */
static int catom(int first) {
  int a;
  int g;
  int n;
  a = pc;
  if (lp_here()) {
    skipop();
    g = 0;
    if (ngrp < 9) { ngrp = ngrp + 1; g = ngrp; emit(I_SAVE, 2 * g, 0); }
    calt();
    if (failed) return a;
    if (!rp_here()) { reerr("unmatched ( in regexp"); return a; }
    skipop();
    if (g) emit(I_SAVE, 2 * g + 1, 0);
    return a;
  }
  if (cp[0] == '[') {
    if (setcnt >= MAXSET) { reerr("too many [ ] in regexp"); return a; }
    emit(I_SET, setcnt, 0);
    cp = readset(cp + 1, setcnt);
    setcnt = setcnt + 1;
    return a;
  }
  if (cp[0] == '.') { emit(I_ANY, 0, 0); cp = cp + 1; return a; }
  if (cp[0] == '^' && (ere || first)) { emit(I_BOL, 0, 0); cp = cp + 1; return a; }
  if (eol_here()) { emit(I_EOL, 0, 0); cp = cp + 1; return a; }
  if (cp[0] == '\\' && cp[1]) {
    n = (unsigned char)cp[1];
    if (n >= '1' && n <= '9') {
      if (ere) { reerr("\\1 .. \\9 is not supported in ERE"); return a; }
      emit(I_BREF, n - '0', 0);
      cp = cp + 2;
      return a;
    }
    if (n == '<' || n == '>' || n == 'b' || n == 'B' || n == 'w' || n == 'W'
        || n == 's' || n == 'S') {
      /* GNU の拡張。**字そのものとして通すと黙って違う答になる**ので
       * 名指しで拒む */
      reerr("\\< \\> \\b \\w \\s are not supported");
      return a;
    }
    emit(I_CHAR, escchar(n), 0);
    cp = cp + 2;
    return a;
  }
  emit(I_CHAR, (unsigned char)cp[0], 0);
  cp = cp + 1;
  return a;
}

/* 節が 1 命令の「1 文字もの」か。繰返しを I_REP に畳めるかの判定 */
static int single(int a) {
  int k;
  if (pc - a != 1) return 0;
  k = iop[a];
  return k == I_CHAR || k == I_ANY || k == I_SET;
}

/* [a,pc) を mn〜mx 回の繰返しでくるむ。mx < 0 は上限なし */
static int wraprep(int a, int mn, int mx) {
  int k;
  int jp;
  int sp;
  int q;
  int i;
  int nopt;
  int pat[MAXOPT];
  if (failed) return -1;
  if (single(a)) {
    if (ins(a, 1) < 0) return -1;
    iop[a] = I_REP;
    ia[a] = mn;
    ib[a] = mx;
    return 0;
  }
  if (mn == 0 && mx < 0) {
    /* * : SPLIT(本体, 抜け); MARK; 本体; JMP 先頭 */
    if (nmark >= MAXMARK) return reerr("regexp too big");
    k = nmark;
    nmark = nmark + 1;
    if (ins(a, 2) < 0) return -1;
    jp = emit(I_JMP, a, 0);
    if (failed) return -1;
    iop[a] = I_SPLIT; ia[a] = a + 2; ib[a] = pc;
    iop[a + 1] = I_MARK; ia[a + 1] = k;
    return 0;
  }
  if (mn == 1 && mx < 0) {
    /* + : MARK; 本体; SPLIT(先頭へ戻る, 抜け) */
    if (nmark >= MAXMARK) return reerr("regexp too big");
    k = nmark;
    nmark = nmark + 1;
    if (ins(a, 1) < 0) return -1;
    iop[a] = I_MARK; ia[a] = k;
    sp = emit(I_SPLIT, a, 0);
    if (failed) return -1;
    ib[sp] = pc;
    return 0;
  }
  if (mn == 0 && mx == 1) {
    if (ins(a, 1) < 0) return -1;
    iop[a] = I_SPLIT; ia[a] = a + 1; ib[a] = pc;
    return 0;
  }
  /* {n,m} : 塊を控えて写す。**写しごとに印を配り直す** (putblk) */
  if (mx >= 0 && mn > mx) return reerr("bad {n,m} in regexp");
  if (mx > MAXOPT || mn > MAXOPT) return reerr("{n,m} is too large");
  if (takeblk(a) < 0) return -1;
  for (i = 0; i < mn; i = i + 1) { if (putblk() < 0) return -1; }
  if (mx < 0) {
    q = pc;
    if (putblk() < 0) return -1;
    return wraprep(q, 0, -1);
  }
  nopt = mx - mn;
  for (i = 0; i < nopt; i = i + 1) {
    pat[i] = emit(I_SPLIT, 0, 0);
    if (failed) return -1;
    ia[pat[i]] = pc;
    if (putblk() < 0) return -1;
  }
  for (i = 0; i < nopt; i = i + 1) ib[pat[i]] = pc;
  return 0;
}

/* 量指定子を 1 つ読む。読めたら 1 を返す */
static int quant(int *mn, int *mx) {
  char *p;
  int n;
  int m;
  if (cp[0] == '*') { cp = cp + 1; *mn = 0; *mx = -1; return 1; }
  if (ere) {
    if (cp[0] == '+') { cp = cp + 1; *mn = 1; *mx = -1; return 1; }
    if (cp[0] == '?') { cp = cp + 1; *mn = 0; *mx = 1; return 1; }
    if (!(cp[0] == '{' && isdi((unsigned char)cp[1]))) return 0;
    p = cp + 1;
  } else {
    if (cp[0] == '\\' && cp[1] == '+') { cp = cp + 2; *mn = 1; *mx = -1; return 1; }
    if (cp[0] == '\\' && cp[1] == '?') { cp = cp + 2; *mn = 0; *mx = 1; return 1; }
    if (!(cp[0] == '\\' && cp[1] == '{')) return 0;
    p = cp + 2;
  }
  n = 0;
  if (!isdi((unsigned char)*p)) { reerr("bad {n,m} in regexp"); return 0; }
  while (isdi((unsigned char)*p)) { n = n * 10 + (*p - '0'); p = p + 1; }
  m = n;
  if (*p == ',') {
    p = p + 1;
    if (isdi((unsigned char)*p)) {
      m = 0;
      while (isdi((unsigned char)*p)) { m = m * 10 + (*p - '0'); p = p + 1; }
    } else {
      m = -1;
    }
  }
  if (ere) {
    if (*p != '}') { reerr("unterminated {"); return 0; }
    p = p + 1;
  } else {
    if (!(p[0] == '\\' && p[1] == '}')) { reerr("unterminated \\{"); return 0; }
    p = p + 2;
  }
  cp = p;
  *mn = n;
  *mx = m;
  return 1;
}

static int ccat(void) {
  int first;
  int a;
  int mn;
  int mx;
  first = 1;
  while (*cp && !rp_here() && !alt_here()) {
    /* BRE では並びの先頭の * は字そのもの (POSIX)。ERE でも同じに
     * 扱う —— 直前の原子が無いので繰返しようがない */
    if (cp[0] == '*' && first) {
      a = pc;
      emit(I_CHAR, '*', 0);
      cp = cp + 1;
    } else {
      a = pc;
      catom(first);
      if (failed) return -1;
      while (quant(&mn, &mx)) {
        if (failed) return -1;
        wraprep(a, mn, mx);
        if (failed) return -1;
      }
      if (failed) return -1;
    }
    first = 0;
  }
  return 0;
}

static int calt(void) {
  int start;
  int jp;
  start = pc;
  ccat();
  while (!failed && alt_here()) {
    havealt = 1;
    skipop();
    if (ins(start, 1) < 0) return start;
    jp = emit(I_JMP, 0, 0);
    if (failed) return start;
    iop[start] = I_SPLIT;
    ia[start] = start + 1;
    ib[start] = pc;
    ccat();
    if (failed) return start;
    ia[jp] = pc;
  }
  return start;
}

static int docompile(char *pat, int isere) {
  int h;
  failed = 0;
  errbuf[0] = 0;
  ere = isere;
  havealt = 0;
  ngrp = 0;                     /* **組の番号は組むたびに 1 から数える** */
  cp = pat;
  h = pc;
  calt();
  if (failed) return -1;
  if (*cp) return reerr("trailing garbage in regexp");
  emit(I_MATCH, 0, 0);
  if (failed) return -1;
  plong[h] = (char)havealt;
  return h;
}

int re_compile(char *pat) { return docompile(pat, 0); }
int re_compile_ere(char *pat) { return docompile(pat, 1); }

int re_reset(void) {
  pc = 0;
  setcnt = 0;
  nmark = 0;
  ngrp = 0;
  overrun = 0;
  return 0;
}

/* ================= 照合する ================= */

static char *linestart;
static char *sav[20];
static char *bsav[20];
static char *mrk[MAXMARK];
static long steps;
static char *best;
static int haveb;
static int longmode;

char *re_gs(int n) { return sav[2 * n]; }
char *re_ge(int n) { return sav[2 * n + 1]; }

/* 1 文字ものの節 1 個。I_REP の中だけで使う */
static int one(int i, char *s) {
  int k;
  k = iop[i];
  if (k == I_CHAR) { if (*s && (unsigned char)*s == ia[i]) return 1; return 0; }
  if (k == I_ANY) { if (*s) return 1; return 0; }
  if (k == I_SET) { if (*s && cset[ia[i]][(unsigned char)*s]) return 1; return 0; }
  return 0;
}

/* 命令列を辿る。1 を返したら「もう探さない」の意である ——
 * 最長を採る形では MATCH でも 0 を返して探し続ける */
static int run(int i, char *s) {
  int op;
  int k;
  int n;
  int mx;
  int r;
  char *old;
  char *b;
  char *e;
  while (1) {
    steps = steps + 1;
    if (steps > STEPLIMIT) { overrun = 1; return 1; }
    op = iop[i];
    if (op == I_CHAR) {
      if (*s && (unsigned char)*s == ia[i]) { s = s + 1; i = i + 1; continue; }
      return 0;
    }
    if (op == I_ANY) {
      if (*s) { s = s + 1; i = i + 1; continue; }
      return 0;
    }
    if (op == I_SET) {
      if (*s && cset[ia[i]][(unsigned char)*s]) { s = s + 1; i = i + 1; continue; }
      return 0;
    }
    if (op == I_BOL) {
      if (s == linestart) { i = i + 1; continue; }
      return 0;
    }
    if (op == I_EOL) {
      if (*s == 0) { i = i + 1; continue; }
      return 0;
    }
    if (op == I_JMP) { i = ia[i]; continue; }
    if (op == I_SPLIT) {
      if (run(ia[i], s)) return 1;
      i = ib[i];
      continue;
    }
    if (op == I_REP) {
      /* 数える繰返し。**長いほうから試す** (貪欲)。ここで再帰が
       * 積まないのが I_REP を置いた理由である */
      n = 0;
      mx = ib[i];
      while (mx < 0 || n < mx) {
        if (!one(i + 1, s + n)) break;
        n = n + 1;
      }
      while (n >= ia[i]) {
        if (run(i + 2, s + n)) return 1;
        n = n - 1;
      }
      return 0;
    }
    if (op == I_SAVE) {
      k = ia[i];
      old = sav[k];
      sav[k] = s;
      r = run(i + 1, s);
      if (r) return r;
      sav[k] = old;
      return 0;
    }
    if (op == I_MARK) {
      /* 空に合う本体で無限に回らないための印。同じ位置へ戻ってきたら
       * その道は打ち切る */
      k = ia[i];
      if (mrk[k] == s) return 0;
      old = mrk[k];
      mrk[k] = s;
      r = run(i + 1, s);
      mrk[k] = old;
      return r;
    }
    if (op == I_BREF) {
      n = ia[i];
      b = sav[2 * n];
      e = sav[2 * n + 1];
      if (b == 0 || e == 0) return 0;
      while (b < e) {
        if (*s == 0 || *s != *b) return 0;
        s = s + 1;
        b = b + 1;
      }
      i = i + 1;
      continue;
    }
    if (op == I_MATCH) {
      if (!haveb || s > best) {
        best = s;
        haveb = 1;
        for (k = 0; k < 20; k = k + 1) bsav[k] = sav[k];
      }
      if (longmode) return 0;   /* 最長を採るので探し続ける */
      return 1;
    }
    return 0;
  }
}

int re_search(int head, char *line, char **mb, char **me) {
  return re_search_from(head, line, line, mb, me);
}

int re_search_from(int head, char *lstart, char *from, char **mb, char **me) {
  char *s;
  int k;
  if (head < 0) return 0;
  linestart = lstart;
  longmode = plong[head];
  steps = 0;
  for (k = 0; k < MAXMARK; k = k + 1) mrk[k] = 0;
  s = from;
  while (1) {
    for (k = 0; k < 20; k = k + 1) { sav[k] = 0; bsav[k] = 0; }
    haveb = 0;
    best = 0;
    run(head, s);
    if (haveb) {
      *mb = s;
      *me = best;
      for (k = 0; k < 20; k = k + 1) sav[k] = bsav[k];
      return 1;
    }
    if (*s == 0) return 0;
    s = s + 1;
  }
}
