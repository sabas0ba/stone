/* sh2.c --- POSIX 部分集合のシェル (Stage 16 第 4 部の 2)
 *
 * 設計は docs/stage016-os.md 10 章。完了条件は tcc の configure
 * (768 行) がそのまま走ることである (9.1)。
 *
 * stage013/sh.c (73 行) とは別物で，複製ではなく新規に書いた。あちらは
 * 「語に分割して spawn」しかせず，構文という概念を持っていない。
 *
 * 作り: 素直な再帰下降で**構文木を作ってから歩く**。こうすると eval と
 * 関数がどちらも「文字列を構文木にして歩く」だけになる (10.2)。
 *
 * 持たないもの (要らないと確かめた。10.1):
 *   経路展開 (glob)   * は case のパターンとパラメータ展開にしか出ない
 *   算術展開 $(( ))   configure に現れない
 *   ジョブ制御・& 　  同上
 *
 * パイプとコマンド置換は**一時ファイルで中継する** (9.3)。カーネルは
 * プロセスを一度に 1 つしか走らせないので，本物のパイプは作れない。
 * 左を流し切ってから右を始めるので「左が終わらないと右が始まらない」
 * が，configure にはその形が現れない。
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- 器の大きさ ---- */
#define NSRC   65536            /* 読み込む脚本の最大 */
#define NNODE  4096             /* 構文木の節 */
#define NWORD  8192             /* 語表 */
#define NSTR   262144           /* 文字列の置き場 */
#define NRD    1024             /* リダイレクト */
#define NVAR   512              /* 変数 */
#define NFUN   64               /* 関数 */
#define NARG   256              /* 1 コマンドの語数 */
#define NBUF   8192             /* 展開の器 */
#define NPOS   64               /* 位置パラメータ */

/* ---- 節の種類 ---- */
#define N_SIMPLE 1
#define N_PIPE   2
#define N_AND    3
#define N_OR     4
#define N_SEQ    5
#define N_IF     6
#define N_WHILE  7
#define N_UNTIL  8
#define N_FOR    9
#define N_CASE   10
#define N_GROUP  11
#define N_FUNC   12
#define N_NOT    13
#define N_CASEIT 14             /* case の 1 項 (パターン列 + 本体) */

/* ---- リダイレクトの種類 ---- */
#define R_IN     1              /* < */
#define R_OUT    2              /* > */
#define R_APP    3              /* >> */
#define R_HERE   4              /* << (本文は word に入れてある) */
#define R_DUP    5              /* >&N / <&N */

struct node {
  int kind;
  int a, b, c;                  /* 子の節 (-1 = 無し) */
  int w0, wn;                   /* 語表の [w0, w0+wn) */
  int r0, rn;                   /* リダイレクト表の [r0, r0+rn) */
  int next;                     /* 並びの次 (case の項・for の本体列) */
};

struct rdir {
  int type;
  int fd;                       /* 左辺の fd (既定は種類で決まる) */
  char *word;
};

struct var {
  char *name;
  char *val;
  int mark;                     /* local の境界を戻すための丈 */
};

struct fun {
  char *name;
  int body;                     /* 節の索引 */
};

static char src[NSRC];
static struct node nd[NNODE];
static int nnd;
static char *wtab[NWORD];
static int nwt;
static char sarena[NSTR];
static int nsa;
static struct rdir rdt[NRD];
static int nrd;
static struct var vars[NVAR];
static int nvar;
static struct fun funs[NFUN];
static int nfun;

/* local の退避。関数に入るときの丈を覚え，出るときにここまで戻す。
 *
 * **変数表の丈を切り詰めるだけでは駄目である。** 関数の中の代入は
 * POSIX では既定で大域なので，local と宣言されたものだけを戻す
 * (configure の assign_opt が関数の中で大域変数を作る) */
#define NLOC 256
struct loc {
  char *name;
  char *old;
  int existed;
};
static struct loc locs[NLOC];
static int nloc;

static char *pos[NPOS];         /* 位置パラメータ $1.. ($0 は pos[0]) */
static int nposn;               /* $# */
static int lastst;              /* $? */
static int tmpseq;              /* 一時ファイルの通し番号 */
static int exiting;             /* exit が呼ばれた */
static int exitst;

/* ---- 文字列の置き場 ---- */

static char *sdup(char *s, int n) {
  char *p;
  if (nsa + n + 1 > NSTR) { fputs("sh2: out of string space\n", stderr); exit(2); }
  p = sarena + nsa;
  memcpy(p, s, (size_t)n);
  p[n] = 0;
  nsa = nsa + n + 1;
  return p;
}

static char *sdup0(char *s) { return sdup(s, (int)strlen(s)); }

/* ---- 変数 ---- */

static struct var *vfind(char *name) {
  int i;
  for (i = nvar - 1; i >= 0; i = i - 1)
    if (strcmp(vars[i].name, name) == 0) return &vars[i];
  return 0;
}

static char *vget(char *name) {
  struct var *v;
  v = vfind(name);
  if (v == 0) return "";
  return v->val;
}

static void vset(char *name, char *val) {
  struct var *v;
  v = vfind(name);
  if (v != 0) { v->val = sdup0(val); return; }
  if (nvar >= NVAR) { fputs("sh2: too many variables\n", stderr); exit(2); }
  vars[nvar].name = sdup0(name);
  vars[nvar].val = sdup0(val);
  vars[nvar].mark = 0;
  nvar = nvar + 1;
}

static void vunset(char *name) {
  struct var *v;
  v = vfind(name);
  if (v != 0) v->val = "";
}

/* ---- 字句 ----
 *
 * 語は**引用符を付けたまま**切り出す。展開のときに引用の状態を追う
 * 必要があるからである (10.3)。
 */
static char *ip;                /* いま読んでいる位置 */
static char *tok;               /* 直前の語 (語のときだけ) */
static int toktype;             /* 0 = 語, それ以外は下の T_* */

#define T_WORD  0
#define T_EOF   1
#define T_SEMI  2               /* ; */
#define T_NL    3               /* 改行 */
#define T_PIPE  4               /* | */
#define T_ANDIF 5               /* && */
#define T_ORIF  6               /* || */
#define T_LP    7               /* ( */
#define T_RP    8               /* ) */
#define T_LB    9               /* { */
#define T_RB    10              /* } */
#define T_LT    11              /* < */
#define T_GT    12              /* > */
#define T_APP   13              /* >> */
#define T_HERE  14              /* << */
#define T_DSEMI 15              /* ;; */
#define T_IONUM 16              /* 数字の直後に < か > が来た */

static int ionum;
/* here-doc の本文を読むと，その語を含む行の改行も食ってしまう。
 * **命令はそこで終わっている**ので，次の lex() で改行を 1 つ返す */
static int pendingnl;

static int isblank2(int c) { return c == ' ' || c == '\t'; }

static int isname1(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int isnamec(int c) { return isname1(c) || (c >= '0' && c <= '9'); }

/* here-doc の本文を集める。ip は << の語の直後の改行の後ろへ進める */
static char *heretext(char *delim) {
  char *out;
  char *st;
  int n;
  int dl;
  out = sarena + nsa;           /* 直に積む */
  dl = (int)strlen(delim);
  /* いまの行の残りを飛ばして次の行から本文 */
  while (*ip && *ip != '\n') ip = ip + 1;
  if (*ip == '\n') ip = ip + 1;
  for (;;) {
    st = ip;
    while (*ip && *ip != '\n') ip = ip + 1;
    n = (int)(ip - st);
    if (*ip == '\n') ip = ip + 1;
    if (n == dl && strncmp(st, delim, (size_t)dl) == 0) break;
    if (n == 0 && *st == 0) break;          /* 終端が見つからないまま尽きた */
    if (nsa + n + 2 > NSTR) { fputs("sh2: here-doc too big\n", stderr); exit(2); }
    memcpy(sarena + nsa, st, (size_t)n);
    nsa = nsa + n;
    sarena[nsa] = '\n';
    nsa = nsa + 1;
    if (*st == 0) break;
  }
  sarena[nsa] = 0;
  nsa = nsa + 1;
  pendingnl = 1;
  return out;
}

/* 次の語 / 記号を取る */
static void lex(void) {
  char *st;
  int q;
  int c;

  if (pendingnl) { pendingnl = 0; toktype = T_NL; return; }
  for (;;) {
    while (isblank2(*ip)) ip = ip + 1;
    if (*ip == '\\' && ip[1] == '\n') { ip = ip + 2; continue; }
    if (*ip == '#') { while (*ip && *ip != '\n') ip = ip + 1; continue; }
    break;
  }
  if (*ip == 0) { toktype = T_EOF; return; }
  c = *ip;
  if (c == '\n') { ip = ip + 1; toktype = T_NL; return; }
  if (c == ';') {
    if (ip[1] == ';') { ip = ip + 2; toktype = T_DSEMI; return; }
    ip = ip + 1; toktype = T_SEMI; return;
  }
  if (c == '|') {
    if (ip[1] == '|') { ip = ip + 2; toktype = T_ORIF; return; }
    ip = ip + 1; toktype = T_PIPE; return;
  }
  if (c == '&') {
    if (ip[1] == '&') { ip = ip + 2; toktype = T_ANDIF; return; }
    ip = ip + 1; toktype = T_SEMI; return;      /* 単独の & は ; と同じ */
  }
  if (c == '(') { ip = ip + 1; toktype = T_LP; return; }
  if (c == ')') { ip = ip + 1; toktype = T_RP; return; }
  if (c == '<') { ip = ip + 1; toktype = T_LT; return; }
  if (c == '>') {
    if (ip[1] == '>') { ip = ip + 2; toktype = T_APP; return; }
    ip = ip + 1; toktype = T_GT; return;
  }

  /* 語。引用符は付けたまま取り込む */
  st = ip;
  q = 0;
  for (;;) {
    c = *ip;
    if (c == 0) break;
    if (q == 0) {
      if (isblank2(c) || c == '\n' || c == ';' || c == '&' || c == '|'
          || c == '<' || c == '>' || c == '(' || c == ')') break;
    }
    if (c == '\\' && ip[1]) { ip = ip + 2; continue; }
    if (q == 0 && (c == '\'' || c == '"')) { q = c; ip = ip + 1; continue; }
    if (q != 0 && c == q) { q = 0; ip = ip + 1; continue; }
    /* $( ... ) と ` ... ` の中は括弧の対応を数えて丸ごと取る */
    if (q != '\'' && c == '$' && ip[1] == '(') {
      int d;
      d = 0;
      ip = ip + 1;
      for (;;) {
        if (*ip == 0) break;
        if (*ip == '(') d = d + 1;
        if (*ip == ')') { d = d - 1; if (d == 0) { ip = ip + 1; break; } }
        ip = ip + 1;
      }
      continue;
    }
    if (q != '\'' && c == '`') {
      ip = ip + 1;
      while (*ip && *ip != '`') {
        if (*ip == '\\' && ip[1]) ip = ip + 1;
        ip = ip + 1;
      }
      if (*ip) ip = ip + 1;
      continue;
    }
    if (q != '\'' && c == '$' && ip[1] == '{') {
      int d;
      d = 0;
      ip = ip + 1;
      for (;;) {
        if (*ip == 0) break;
        if (*ip == '{') d = d + 1;
        if (*ip == '}') { d = d - 1; if (d == 0) { ip = ip + 1; break; } }
        ip = ip + 1;
      }
      continue;
    }
    ip = ip + 1;
  }
  tok = sdup(st, (int)(ip - st));
  toktype = T_WORD;
  /* "2>" のような fd 付きリダイレクト */
  if ((*ip == '>' || *ip == '<') && tok[0] >= '0' && tok[0] <= '9'
      && tok[1] == 0) {
    ionum = tok[0] - '0';
    toktype = T_IONUM;
  }
}

/* 先読み 1 個ぶんの巻き戻し */
static char *savedip;
static char *savedtok;
static int savedtype;
static void lmark(void) { savedip = ip; savedtok = tok; savedtype = toktype; }
static void lback(void) { ip = savedip; tok = savedtok; toktype = savedtype; }

/* ---- 構文解析 (再帰下降) ----
 *
 * list     := and_or ((';' | '\n') and_or)*
 * and_or   := pipeline (('&&' | '||') pipeline)*
 * pipeline := command ('|' command)*
 * command  := simple | if | while | until | for | case | '{' list '}' | funcdef
 */

static int p_list(void);

static int newnode(int kind) {
  int n;
  if (nnd >= NNODE) { fputs("sh2: script too complex\n", stderr); exit(2); }
  n = nnd;
  nnd = nnd + 1;
  nd[n].kind = kind;
  nd[n].a = -1; nd[n].b = -1; nd[n].c = -1;
  nd[n].w0 = nwt; nd[n].wn = 0;
  nd[n].r0 = nrd; nd[n].rn = 0;
  nd[n].next = -1;
  return n;
}

static void addword(char *w) {
  if (nwt >= NWORD) { fputs("sh2: too many words\n", stderr); exit(2); }
  wtab[nwt] = w;
  nwt = nwt + 1;
}

static void addrd(int type, int fd, char *w) {
  if (nrd >= NRD) { fputs("sh2: too many redirections\n", stderr); exit(2); }
  rdt[nrd].type = type;
  rdt[nrd].fd = fd;
  rdt[nrd].word = w;
  nrd = nrd + 1;
}

/* 改行と ; を読み飛ばす */
static void skipnl(void) {
  while (toktype == T_NL || toktype == T_SEMI) lex();
}

static int iskw(char *s) {
  return toktype == T_WORD && strcmp(tok, s) == 0;
}

/* 「並びを終わらせる語」。**if / while / for / case は入らない** ——
 * それらは命令を**始める**語なので，並びの途中に出てきたら次の命令で
 * ある。ここを混ぜると for opt do ... case ... esac ... done の case で
 * 並びが切れて "expected done" になる (実際になった) */
static int isterm(char *s) {
  return strcmp(s, "then") == 0 || strcmp(s, "elif") == 0
      || strcmp(s, "else") == 0 || strcmp(s, "fi") == 0
      || strcmp(s, "do") == 0 || strcmp(s, "done") == 0
      || strcmp(s, "esac") == 0 || strcmp(s, "}") == 0
      || strcmp(s, "in") == 0;
}

/* 「命令の名前にはならない語」。始める語も終わらせる語も含む */
static int isreserved(char *s) {
  return strcmp(s, "if") == 0 || strcmp(s, "then") == 0
      || strcmp(s, "elif") == 0 || strcmp(s, "else") == 0
      || strcmp(s, "fi") == 0 || strcmp(s, "while") == 0
      || strcmp(s, "until") == 0 || strcmp(s, "do") == 0
      || strcmp(s, "done") == 0 || strcmp(s, "for") == 0
      || strcmp(s, "in") == 0 || strcmp(s, "case") == 0
      || strcmp(s, "esac") == 0 || strcmp(s, "}") == 0;
}

/* 誤りの位置を行番号で出す。ip が脚本の中にあるときだけ数える
 * (eval や関数の本体は別の器なので数えられない) */
static int curline(void) {
  char *q;
  int n;
  if (ip < src || ip > src + NSRC) return 0;
  n = 1;
  for (q = src; q < ip && *q; q = q + 1) if (*q == '\n') n = n + 1;
  return n;
}

static void syerr(char *what) {
  int ln;
  ln = curline();
  if (ln > 0) fprintf(stderr, "sh2: %d: syntax error: %s\n", ln, what);
  else fprintf(stderr, "sh2: syntax error: %s\n", what);
  if (toktype == T_WORD) fprintf(stderr, "sh2:   near word '%s'\n", tok);
  exit(2);
}

static void expect(char *s) {
  if (!iskw(s)) {
    char b[64];
    sprintf(b, "expected %s", s);
    syerr(b);
  }
  lex();
}

/* リダイレクトを 1 つ読む。読んだら 1 */
static int p_redir(int *rn) {
  int fd;
  int type;
  fd = -1;
  if (toktype == T_IONUM) { fd = ionum; lex(); }
  if (toktype == T_LT) {
    if (ip[0] == '<') {         /* << (字句が < を 2 つに割っている) */
      ip = ip + 1;
      lex();
      addrd(R_HERE, fd < 0 ? 0 : fd, heretext(tok));
      lex();
      *rn = *rn + 1;
      return 1;
    }
    type = R_IN;
  } else if (toktype == T_GT) {
    type = R_OUT;
  } else if (toktype == T_APP) {
    type = R_APP;
  } else {
    if (fd >= 0) syerr("redirect fd");
    return 0;
  }
  /* N>&M / N<&M。lex は単独の & を T_SEMI にするので，字句より前の
   * 生の文字を見て判断する */
  if (*ip == '&') {
    ip = ip + 1;
    lex();
    if (fd < 0) fd = (type == R_IN) ? 0 : 1;
    addrd(R_DUP, fd, (toktype == T_WORD) ? tok : sdup0("1"));
    lex();
    *rn = *rn + 1;
    return 1;
  }
  lex();
  if (toktype != T_WORD) syerr("redirect target");
  if (fd < 0) fd = (type == R_IN) ? 0 : 1;
  addrd(type, fd, tok);
  lex();
  *rn = *rn + 1;
  return 1;
}

static int p_simple(void) {
  int n;
  int rn;
  n = newnode(N_SIMPLE);
  rn = 0;
  for (;;) {
    if (p_redir(&rn)) continue;
    if (toktype != T_WORD) break;
    if (nd[n].wn == 0 && isreserved(tok)) break;
    addword(tok);
    nd[n].wn = nd[n].wn + 1;
    lex();
  }
  nd[n].rn = rn;
  if (nd[n].wn == 0 && rn == 0) return -1;
  return n;
}

static int p_if(void) {
  int n;
  int c;
  n = newnode(N_IF);
  lex();                        /* if */
  nd[n].a = p_list();           /* 条件 */
  expect("then");
  nd[n].b = p_list();           /* then 側 */
  if (iskw("elif")) {
    /* elif は「else の中に if がある」ものとして畳む */
    nd[n].c = p_if();
    return n;
  }
  if (iskw("else")) {
    lex();
    nd[n].c = p_list();
  }
  if (iskw("fi")) { lex(); return n; }
  /* elif から来た場合は fi を親が食う */
  c = 0;
  (void)c;
  return n;
}

static int p_while(int kind) {
  int n;
  n = newnode(kind);
  lex();                        /* while / until */
  nd[n].a = p_list();
  expect("do");
  nd[n].b = p_list();
  expect("done");
  return n;
}

static int p_for(void) {
  int n;
  n = newnode(N_FOR);
  lex();                        /* for */
  if (toktype != T_WORD) syerr("for");
  addword(tok);                 /* 変数名 (w0) */
  nd[n].wn = 1;
  lex();
  skipnl();
  if (iskw("in")) {
    lex();
    while (toktype == T_WORD && !isreserved(tok)) {
      addword(tok);
      nd[n].wn = nd[n].wn + 1;
      lex();
    }
  } else {
    /* in が無ければ位置パラメータを回す。"$@" を 1 語として入れておく */
    addword(sdup0("\"$@\""));
    nd[n].wn = nd[n].wn + 1;
  }
  skipnl();
  expect("do");
  nd[n].b = p_list();
  expect("done");
  return n;
}

static int p_case(void) {
  int n;
  int it;
  int prev;
  n = newnode(N_CASE);
  lex();                        /* case */
  if (toktype != T_WORD) syerr("case");
  addword(tok);                 /* 対象の語 */
  nd[n].wn = 1;
  lex();
  skipnl();
  expect("in");
  skipnl();
  prev = -1;
  while (!iskw("esac") && toktype != T_EOF) {
    it = newnode(N_CASEIT);
    if (toktype == T_LP) lex();         /* 省略できる ( */
    for (;;) {
      if (toktype != T_WORD) break;
      addword(tok);
      nd[it].wn = nd[it].wn + 1;
      lex();
      if (toktype == T_PIPE) { lex(); continue; }
      break;
    }
    if (toktype != T_RP) syerr("case pattern");
    lex();
    skipnl();
    if (!iskw("esac") && toktype != T_DSEMI)
      nd[it].a = p_list();
    if (toktype == T_DSEMI) lex();
    skipnl();
    if (prev < 0) nd[n].a = it; else nd[prev].next = it;
    prev = it;
  }
  expect("esac");
  return n;
}

/* 複合コマンドの後ろに付くリダイレクトを拾う。
 * **case や if にも付く** —— configure の 611 行目が
 *   case $source_path in ... esac >>config.mak
 * である。簡単コマンドだけ見ていると，ここの出力が端末へ漏れる
 * (実際に TOPSRC= の行が config.mak に入らなかった) */
static void trailrd(int n) {
  int rn;
  int r0;
  r0 = nrd;
  rn = 0;
  while (p_redir(&rn)) ;
  if (rn > 0) { nd[n].r0 = r0; nd[n].rn = rn; }
}

static int p_command(void) {
  int n;
  int rn;

  skipnl();
  if (toktype == T_EOF) return -1;
  if (iskw("if")) { n = p_if(); trailrd(n); return n; }
  if (iskw("while")) { n = p_while(N_WHILE); trailrd(n); return n; }
  if (iskw("until")) { n = p_while(N_UNTIL); trailrd(n); return n; }
  if (iskw("for")) { n = p_for(); trailrd(n); return n; }
  if (iskw("case")) { n = p_case(); trailrd(n); return n; }
  if (iskw("!")) { lex(); n = newnode(N_NOT); nd[n].a = p_command(); return n; }
  if (toktype == T_WORD && strcmp(tok, "{") == 0) {
    lex();
    n = newnode(N_GROUP);
    nd[n].a = p_list();
    expect("}");
    rn = 0;
    while (p_redir(&rn)) ;
    nd[n].rn = rn;
    return n;
  }
  if (toktype == T_LP) {
    /* サブシェルは並行しないので群と同じに扱う */
    lex();
    n = newnode(N_GROUP);
    nd[n].a = p_list();
    if (toktype != T_RP) syerr(")");
    lex();
    return n;
  }
  /* 関数定義か? name () { ... } */
  if (toktype == T_WORD && isname1((int)(unsigned char)tok[0])) {
    lmark();
    {
      char *nm;
      nm = tok;
      lex();
      if (toktype == T_LP) {
        lex();
        if (toktype == T_RP) {
          lex();
          skipnl();
          n = newnode(N_FUNC);
          addword(nm);
          nd[n].wn = 1;
          nd[n].a = p_command();
          return n;
        }
      }
      lback();
    }
  }
  return p_simple();
}

static int p_pipeline(void) {
  int n;
  int l;
  l = p_command();
  if (l < 0) return -1;
  while (toktype == T_PIPE) {
    lex();
    skipnl();
    n = newnode(N_PIPE);
    nd[n].a = l;
    nd[n].b = p_command();
    l = n;
  }
  return l;
}

static int p_andor(void) {
  int n;
  int l;
  l = p_pipeline();
  if (l < 0) return -1;
  for (;;) {
    if (toktype == T_ANDIF) {
      lex(); skipnl();
      n = newnode(N_AND);
    } else if (toktype == T_ORIF) {
      lex(); skipnl();
      n = newnode(N_OR);
    } else {
      break;
    }
    nd[n].a = l;
    nd[n].b = p_pipeline();
    l = n;
  }
  return l;
}

static int p_list(void) {
  int n;
  int l;
  skipnl();
  l = p_andor();
  if (l < 0) return -1;
  for (;;) {
    if (toktype != T_SEMI && toktype != T_NL) break;
    while (toktype == T_SEMI || toktype == T_NL) lex();
    if (toktype == T_EOF) break;
    if (toktype == T_WORD && isterm(tok)) break;
    if (toktype == T_RP || toktype == T_DSEMI) break;
    n = newnode(N_SEQ);
    nd[n].a = l;
    nd[n].b = p_andor();
    if (nd[n].b < 0) { nnd = nnd - 1; break; }
    l = n;
  }
  return l;
}

/* ---- 語の展開 ----
 *
 * POSIX の順序を守る (10.3)。
 *   1. パラメータ展開  2. コマンド置換  3. 語分割  4. 引用の除去
 *
 * 3 が効くのは**引用の外から来た部分だけ**である。"$1" は切らないが
 * $1 は切る。したがって展開しながら「この文字は引用の中から来たか」を
 * 対で持ち歩く (ebuf と eqf)。
 */
/* here-doc の本文を展開するときは，引用符を**文字として**扱う。
 * POSIX では終端子が引用されていなければ $ と ` と \ だけが特別で，
 * ' と " は素の文字である */
static int noquote;

/* この語に引用が現れたか。**引用のあった空語は 1 個の空引数になる。**
 * "$CC" で CC が未設定なら「空の引数が 1 つ」であって「引数なし」では
 * ない。ここを落とすと test -n "$CC" が test -n になり，引数 1 個の
 * test として**真になってしまう** (configure 57 行目でこれを踏んだ) */
static int wquoted;

/* この語で "$@" を展開したか。**"$@" は位置パラメータが無ければ
 * 0 個の語になる** (POSIX の特例)。"$X" が空なら 1 個の空語なので，
 * 両者を分けて扱う必要がある */
static int sawat;

static char ebuf[NBUF];
static char eqf[NBUF];          /* 1 = 引用の中から来た */
static int elen;

static char *argv_[NARG];
static int argc_;

static int runtree(int n);      /* 前方宣言 */
static int runstr(char *s);
static int patmatch(char *pat, char *s);
static void expandfrag(char *w, int q0);

static void eput(int c, int q) {
  if (elen >= NBUF - 1) { fputs("sh2: word too long\n", stderr); exit(2); }
  ebuf[elen] = (char)c;
  eqf[elen] = (char)q;
  elen = elen + 1;
}

static void eputs(char *s, int q) {
  while (*s) { eput((int)(unsigned char)*s, q); s = s + 1; }
}

/* 一時ファイルの名前を作る */
static void tmpname(char *out) {
  tmpseq = tmpseq + 1;
  sprintf(out, "/tmp.sh2.%d", tmpseq);
}

/* 中身を丸ごと読んで返す (末尾の改行は落とす) */
static char *slurpfile(char *path, int strip) {
  int fd;
  int n;
  char *out;
  fd = open(path, O_RDONLY);
  if (fd < 0) return sdup0("");
  out = sarena + nsa;
  for (;;) {
    if (nsa + 4096 + 1 > NSTR) { fputs("sh2: out of string space\n", stderr); exit(2); }
    n = read(fd, sarena + nsa, 4096);
    if (n <= 0) break;
    nsa = nsa + n;
  }
  close(fd);
  if (strip) while (nsa > 0 && sarena[nsa - 1] == '\n') nsa = nsa - 1;
  sarena[nsa] = 0;
  nsa = nsa + 1;
  return out;
}

/* 出力の行き先。**カーネルに dup2 が無い**ので，fd を差し替える代わりに
 * 「いまの出力先のファイル名」を持ち回す。
 *
 * 外部コマンドは spawn(path, argv, in, out) がファイル名を受けるので
 * そのまま渡せる (docs/stage013-tools.md 3.2)。組込みは outs() を通して
 * 書き，ここを見て振り分ける。 */
static char *curout;            /* 0 なら標準出力 */
static int curapp;              /* 1 なら追記 */
static char *curin;             /* 0 なら標準入力 */

/* 組込みの出力。curout が立っていればそこへ足す */
static void outs(char *s) {
  int fd;
  int n;
  if (curout == 0) { fputs(s, stdout); return; }
  fd = open(curout, O_WRONLY | O_CREAT | (curapp ? 0 : O_TRUNC), 0666);
  if (fd < 0) return;
  if (curapp) lseek(fd, 0, 2);
  n = (int)strlen(s);
  write(fd, s, (size_t)n);
  close(fd);
  curapp = 1;                   /* 同じ実行の続きは足していく */
}

/* コマンド置換: 中身を一時ファイルへ流して読み戻す (9.3)。
 *
 * **展開の途中から実行へ降りるので，展開の器を退避する。** ebuf は
 * 呼び手 (expand1) が組み立てている最中であり，argv_ も呼び手
 * (runsimple) が積んでいる最中である。中で走るコマンドがそれらを
 * 使い回すので，保存しないと外側の語が壊れる。
 *
 * 最初これを忘れて `echo cmd=$(echo sub)` が "sub subsub" になった。 */
static char *cmdsub(char *body) {
  char tn[64];
  char sbuf[NBUF];
  char sqf[NBUF];
  char *sav[NARG];
  char *res;
  char *so;
  int sa;
  int sl;
  int sac;
  int i;

  /* 展開の器を退避 */
  sl = elen;
  for (i = 0; i < sl; i = i + 1) { sbuf[i] = ebuf[i]; sqf[i] = eqf[i]; }
  sac = argc_;
  for (i = 0; i < sac && i < NARG; i = i + 1) sav[i] = argv_[i];

  tmpname(tn);
  so = curout; sa = curapp;
  curout = sdup0(tn); curapp = 0;
  runstr(body);
  curout = so; curapp = sa;
  res = slurpfile(tn, 1);
  unlink(tn);

  /* 戻す */
  elen = sl;
  for (i = 0; i < sl; i = i + 1) { ebuf[i] = sbuf[i]; eqf[i] = sqf[i]; }
  argc_ = sac;
  for (i = 0; i < sac && i < NARG; i = i + 1) argv_[i] = sav[i];
  return res;
}

/* ${NAME} の中身を処理する。s は '{' の次から '}' の手前まで */
static void expandbrace(char *s, int n, int q) {
  char name[128];
  int i;
  int op;
  int opl;
  char *val;
  char *arg;
  char *e;

  /* 演算子 (#, ##, %, %%, :-, :=, :?, :+) を探す。名前は英数字と _ */
  i = 0;
  while (i < n && (isnamec((int)(unsigned char)s[i]) || (i == 0 && (s[i] == '#' || s[i] == '?' || s[i] == '*' || s[i] == '@'))))
    i = i + 1;
  if (i > 127) i = 127;
  memcpy(name, s, (size_t)i);
  name[i] = 0;

  op = 0;
  opl = 0;
  arg = "";
  if (i < n) {
    if (s[i] == ':' && i + 1 < n) { op = s[i + 1]; opl = 2; }
    else if (s[i] == '#' && i + 1 < n && s[i + 1] == '#') { op = 'H'; opl = 2; }
    else if (s[i] == '%' && i + 1 < n && s[i + 1] == '%') { op = 'P'; opl = 2; }
    else if (s[i] == '#') { op = 'h'; opl = 1; }
    else if (s[i] == '%') { op = 'p'; opl = 1; }
    if (op != 0) {
      arg = sdup(s + i + opl, n - i - opl);
    }
  }

  /* 名前の値を取る */
  if (strcmp(name, "#") == 0) {
    char nb[16];
    sprintf(nb, "%d", nposn);
    val = sdup0(nb);
  } else if (strcmp(name, "?") == 0) {
    char nb[16];
    sprintf(nb, "%d", lastst);
    val = sdup0(nb);
  } else if (name[0] >= '0' && name[0] <= '9' && name[1] == 0) {
    int k;
    k = name[0] - '0';
    val = (k <= nposn && pos[k] != 0) ? pos[k] : "";
  } else {
    val = vget(name);
  }

  /* 演算子を当てる */
  if (op == '-') {              /* ${V:-既定} */
    /* 既定は名前ではなく**語**である。$(...) や ${...} が入れ子で
     * 現れうるので断片として展開する (configure 69 行目の
     * ${2:-${1%%=*}} がこれ) */
    if (val[0] == 0) { expandfrag(arg, q); return; }
  } else if (op == '=') {       /* ${V:=既定} */
    if (val[0] == 0) {
      int sl;
      int i2;
      char sb[NBUF];
      sl = elen;
      for (i2 = 0; i2 < sl; i2 = i2 + 1) sb[i2] = ebuf[i2];
      elen = 0;
      expandfrag(arg, 0);
      eput(0, 1);
      vset(name, ebuf);
      elen = sl;
      for (i2 = 0; i2 < sl; i2 = i2 + 1) ebuf[i2] = sb[i2];
      val = vget(name);
    }
  } else if (op == 'h' || op == 'H') {          /* # / ## 前を落とす */
    int al;
    al = (int)strlen(arg);
    /* パターンは * を含みうる。* を含まない単純な場合だけ字面で比べ，
     * 含む場合は最短 (h) / 最長 (H) の一致を探す */
    if (strchr(arg, '*') == 0) {
      if (al > 0 && strncmp(val, arg, (size_t)al) == 0) val = val + al;
    } else {
      int vl;
      int k;
      int st;
      vl = (int)strlen(val);
      st = -1;
      for (k = (op == 'h') ? 0 : vl; (op == 'h') ? k <= vl : k >= 0;
           k = (op == 'h') ? k + 1 : k - 1) {
        char sav;
        sav = val[k];
        val[k] = 0;
        if (patmatch(arg, val)) st = k;
        val[k] = sav;
        if (st >= 0) break;
      }
      if (st >= 0) val = val + st;
    }
  } else if (op == 'p' || op == 'P') {          /* % / %% 後ろを落とす */
    int vl;
    int k;
    int cut;
    vl = (int)strlen(val);
    cut = -1;
    for (k = (op == 'p') ? vl : 0; (op == 'p') ? k >= 0 : k <= vl;
         k = (op == 'p') ? k - 1 : k + 1) {
      if (patmatch(arg, val + k)) { cut = k; break; }
    }
    if (cut >= 0) {
      val = sdup(val, cut);
    }
  }

  /* $* と $@ は位置パラメータを並べる */
  if (strcmp(name, "*") == 0 || strcmp(name, "@") == 0) {
    int k;
    if (name[0] == '@') sawat = 1;
    for (k = 1; k <= nposn; k = k + 1) {
      if (k > 1) eput(' ', q ? 0 : 0);   /* 引用の中でも語の切れ目にする */
      eputs(pos[k] ? pos[k] : "", q);
    }
    return;
  }
  e = val;
  eputs(e, q);
}

/* 断片を展開して ebuf/eqf へ**足す** (器は初期化しない)。
 * ${V:-既定} の既定側がこれを使う —— 既定は名前ではなく語なので，
 * $(...) や ${...} が入れ子で現れうる */
static void expandfrag(char *w, int q0) {
  int q;                        /* 0 = 素, '\'' か '"' */
  char *p;
  q = q0 ? '"' : 0;             /* 引用の中から呼ばれたら引用として扱う */
  p = w;
  while (*p) {
    int c;
    c = (int)(unsigned char)*p;
    if (c == '\\' && p[1] == '\n' && q != '\'') {
      /* 逆斜線 + 改行は**行の継続**である。何も出さずに飲む。
       * here-doc の中で長い行を折る書き方に出てくる (config.h の
       * #if の行がこれで，飲まないと 2 行に割れる) */
      p = p + 2;
      continue;
    }
    if (c == '\\' && p[1] && q != '\'') {
      /* 二重引用符の中では $ ` " \\ と改行の前だけが特別である (POSIX)。
       * それ以外は**逆斜線をそのまま残す** —— "\n" を tr に渡す形が
       * configure に出てくるので，ここで落とすと壊れる */
      if (q == '"' && p[1] != '$' && p[1] != '`' && p[1] != '"'
          && p[1] != '\\' && p[1] != '\n') {
        eput('\\', 1);
        eput((int)(unsigned char)p[1], 1);
        p = p + 2;
        continue;
      }
      eput((int)(unsigned char)p[1], 1);
      p = p + 2;
      continue;
    }
    if (!noquote && q == 0 && (c == '\'' || c == '"')) {
      q = c; wquoted = 1; p = p + 1; continue;
    }
    if (!noquote && q != 0 && c == q) { q = 0; p = p + 1; continue; }
    if (q != '\'' && c == '`') {
      char *st;
      p = p + 1;
      st = p;
      while (*p && *p != '`') p = p + 1;
      eputs(cmdsub(sdup(st, (int)(p - st))), q ? 1 : 0);
      if (*p) p = p + 1;
      continue;
    }
    if (q != '\'' && c == '$' && p[1] == '(') {
      char *st;
      int d;
      p = p + 2;
      st = p;
      d = 1;
      while (*p) {
        if (*p == '(') d = d + 1;
        if (*p == ')') { d = d - 1; if (d == 0) break; }
        p = p + 1;
      }
      eputs(cmdsub(sdup(st, (int)(p - st))), q ? 1 : 0);
      if (*p) p = p + 1;
      continue;
    }
    if (q != '\'' && c == '$' && p[1] == '{') {
      char *st;
      int d;
      p = p + 2;
      st = p;
      d = 1;
      while (*p) {
        if (*p == '{') d = d + 1;
        if (*p == '}') { d = d - 1; if (d == 0) break; }
        p = p + 1;
      }
      expandbrace(st, (int)(p - st), q ? 1 : 0);
      if (*p) p = p + 1;
      continue;
    }
    if (q != '\'' && c == '$' && p[1]) {
      char nm[128];
      int k;
      p = p + 1;
      k = 0;
      if (*p == '#' || *p == '?' || *p == '*' || *p == '@'
          || (*p >= '0' && *p <= '9')) {
        nm[0] = *p; nm[1] = 0; p = p + 1;
      } else if (isname1((int)(unsigned char)*p)) {
        while (isnamec((int)(unsigned char)*p) && k < 127) {
          nm[k] = *p; k = k + 1; p = p + 1;
        }
        nm[k] = 0;
      } else {
        eput('$', q ? 1 : 0);
        continue;
      }
      expandbrace(nm, (int)strlen(nm), q ? 1 : 0);
      continue;
    }
    eput(c, q ? 1 : 0);
    p = p + 1;
  }
}

/* 1 語を展開して ebuf/eqf へ置く。分割はしない */
static void expand1(char *w) {
  elen = 0;
  wquoted = 0;
  sawat = 0;
  expandfrag(w, 0);
}

/* 展開した ebuf を IFS で切って argv_ へ積む。
 * **引用の中から来た文字 (eqf) では切らない** —— これが 10.3 の肝 */
static void splitadd(void) {
  int i;
  int st;
  int got;
  got = 0;
  i = 0;
  while (i < elen) {
    while (i < elen && eqf[i] == 0
           && (ebuf[i] == ' ' || ebuf[i] == '\t' || ebuf[i] == '\n'))
      i = i + 1;
    if (i >= elen) break;
    st = i;
    while (i < elen && !(eqf[i] == 0
           && (ebuf[i] == ' ' || ebuf[i] == '\t' || ebuf[i] == '\n')))
      i = i + 1;
    if (argc_ >= NARG) { fputs("sh2: too many arguments\n", stderr); exit(2); }
    argv_[argc_] = sdup(ebuf + st, i - st);
    argc_ = argc_ + 1;
    got = 1;
  }
  /* 引用があったのに何も出なかったら，空の引数を 1 つ置く。
   * ただし "$@" だけは例外で，位置パラメータが無ければ 0 個である */
  if (!got && wquoted && !sawat) {
    if (argc_ >= NARG) { fputs("sh2: too many arguments\n", stderr); exit(2); }
    argv_[argc_] = sdup0("");
    argc_ = argc_ + 1;
  }
}

/* 語を 1 つ展開して argv_ へ足す (分割あり) */
static void addarg(char *w) {
  expand1(w);
  splitadd();
}

/* 語を 1 つ展開して 1 語のまま返す (リダイレクトの先など) */
static char *expandone(char *w) {
  expand1(w);
  return sdup(ebuf, elen);
}

/* ---- パターン照合 (case と ${V#...} が使う) ----
 *
 * 経路展開は要らない (10.1) ので，**文字列に対する照合だけ**でよい。
 * * があるので後戻りが要る。素直に再帰で書く。
 */
static int patmatch(char *pat, char *s) {
  for (;;) {
    if (*pat == 0) return *s == 0;
    if (*pat == '*') {
      pat = pat + 1;
      if (*pat == 0) return 1;
      while (*s) {
        if (patmatch(pat, s)) return 1;
        s = s + 1;
      }
      return patmatch(pat, s);
    }
    if (*s == 0) return 0;
    if (*pat == '?') { pat = pat + 1; s = s + 1; continue; }
    if (*pat == '[') {
      int neg;
      int hit;
      char *q;
      q = pat + 1;
      neg = 0;
      if (*q == '!' || *q == '^') { neg = 1; q = q + 1; }
      hit = 0;
      for (;;) {
        if (*q == 0) return 0;                  /* 閉じない [ */
        if (*q == ']' && q != pat + 1 + neg) break;
        if (q[1] == '-' && q[2] && q[2] != ']') {
          if (*s >= q[0] && *s <= q[2]) hit = 1;
          q = q + 3;
        } else {
          if (*s == *q) hit = 1;
          q = q + 1;
        }
      }
      if (neg) hit = !hit;
      if (!hit) return 0;
      pat = q + 1;
      s = s + 1;
      continue;
    }
    if (*pat == '\\' && pat[1]) pat = pat + 1;
    if (*pat != *s) return 0;
    pat = pat + 1;
    s = s + 1;
  }
}

/* ---- 組込み ---- */

static int nposave;
static char *posave[NPOS];

static int b_test(int ac, char **av);

/* 数を文字列にして返す */
static char *itos(int v) {
  char b[16];
  sprintf(b, "%d", v);
  return sdup0(b);
}

static int b_echo(int ac, char **av) {
  int i;
  int nl;
  i = 1;
  nl = 1;
  if (i < ac && strcmp(av[i], "-n") == 0) { nl = 0; i = i + 1; }
  for (; i < ac; i = i + 1) {
    outs(av[i]);
    if (i + 1 < ac) outs(" ");
  }
  if (nl) outs("\n");
  return 0;
}

static int b_cd(int ac, char **av) {
  char *d;
  d = (ac > 1) ? av[1] : vget("HOME");
  if (d[0] == 0) d = "/";
  if (chdir(d) < 0) { fputs("cd: no such directory\n", stderr); return 1; }
  return 0;
}

static int b_pwd(int ac, char **av) {
  char b[512];
  (void)ac; (void)av;
  if (getcwd(b, 512) == 0) return 1;
  outs(b);
  outs("\n");
  return 0;
}

static int b_set(int ac, char **av) {
  int i;
  int k;
  if (ac == 1) return 0;
  i = 1;
  if (strcmp(av[1], "--") == 0) i = 2;
  else if (av[1][0] == '-') return 0;           /* set -e などは黙って無視 */
  k = 0;
  for (; i < ac && k + 1 < NPOS; i = i + 1) {
    k = k + 1;
    pos[k] = av[i];
  }
  nposn = k;
  return 0;
}

static int b_shift(int ac, char **av) {
  int n;
  int i;
  n = (ac > 1) ? atoi(av[1]) : 1;
  if (n > nposn) return 1;
  for (i = 1; i + n <= nposn; i = i + 1) pos[i] = pos[i + n];
  nposn = nposn - n;
  return 0;
}

static int b_unset(int ac, char **av) {
  int i;
  for (i = 1; i < ac; i = i + 1) vunset(av[i]);
  return 0;
}

static int b_eval(int ac, char **av) {
  int i;
  int n;
  char *buf;
  n = 0;
  for (i = 1; i < ac; i = i + 1) n = n + (int)strlen(av[i]) + 1;
  buf = sarena + nsa;
  if (nsa + n + 2 > NSTR) { fputs("sh2: out of string space\n", stderr); exit(2); }
  buf[0] = 0;
  for (i = 1; i < ac; i = i + 1) {
    if (i > 1) strcat(buf, " ");
    strcat(buf, av[i]);
  }
  nsa = nsa + n + 1;
  return runstr(buf);
}

static int b_read(int ac, char **av) {
  char line[4096];
  if (fgets(line, 4096, stdin) == 0) return 1;
  {
    int n;
    n = (int)strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n = n - 1;
    line[n] = 0;
  }
  if (ac > 1) vset(av[1], line);
  return 0;
}

static int b_exit(int ac, char **av) {
  exiting = 1;
  exitst = (ac > 1) ? atoi(av[1]) : lastst;
  return exitst;
}

/* test / [ 。configure が使う演算だけを持つ */
static int b_test(int ac, char **av) {
  int n;
  n = ac;
  if (strcmp(av[0], "[") == 0) {
    if (n > 1 && strcmp(av[n - 1], "]") == 0) n = n - 1;
  }
  if (n <= 1) return 1;
  /* ! を剥がす */
  if (strcmp(av[1], "!") == 0) {
    char *sub[NARG];
    int i;
    sub[0] = av[0];
    for (i = 2; i < n; i = i + 1) sub[i - 1] = av[i];
    return b_test(n - 1, sub) ? 0 : 1;
  }
  if (n == 2) return av[1][0] == 0 ? 1 : 0;             /* test STR */
  if (n == 3) {
    if (strcmp(av[1], "-z") == 0) return av[2][0] == 0 ? 0 : 1;
    if (strcmp(av[1], "-n") == 0) return av[2][0] != 0 ? 0 : 1;
    if (strcmp(av[1], "-f") == 0 || strcmp(av[1], "-r") == 0
        || strcmp(av[1], "-e") == 0 || strcmp(av[1], "-s") == 0) {
      int fd;
      fd = open(av[2], O_RDONLY);
      if (fd < 0) return 1;
      close(fd);
      return 0;
    }
    if (strcmp(av[1], "-d") == 0) {
      /* ディレクトリは open できるが read が EISDIR になる (kernel20) */
      int fd;
      char b[1];
      fd = open(av[2], O_RDONLY);
      if (fd < 0) return 1;
      {
        int r;
        r = read(fd, b, 1);
        close(fd);
        return (r < 0) ? 0 : 1;
      }
    }
    return 1;
  }
  if (n == 4) {
    if (strcmp(av[2], "=") == 0) return strcmp(av[1], av[3]) == 0 ? 0 : 1;
    if (strcmp(av[2], "!=") == 0) return strcmp(av[1], av[3]) != 0 ? 0 : 1;
    if (strcmp(av[2], "-eq") == 0) return atoi(av[1]) == atoi(av[3]) ? 0 : 1;
    if (strcmp(av[2], "-ne") == 0) return atoi(av[1]) != atoi(av[3]) ? 0 : 1;
    if (strcmp(av[2], "-lt") == 0) return atoi(av[1]) < atoi(av[3]) ? 0 : 1;
    if (strcmp(av[2], "-le") == 0) return atoi(av[1]) <= atoi(av[3]) ? 0 : 1;
    if (strcmp(av[2], "-gt") == 0) return atoi(av[1]) > atoi(av[3]) ? 0 : 1;
    if (strcmp(av[2], "-ge") == 0) return atoi(av[1]) >= atoi(av[3]) ? 0 : 1;
    return 1;
  }
  return 1;
}

/* 組込みなら実行して 1 を返す */
static int builtin(int ac, char **av, int *st) {
  char *c;
  c = av[0];
  if (strcmp(c, ":") == 0 || strcmp(c, "true") == 0) { *st = 0; return 1; }
  if (strcmp(c, "false") == 0) { *st = 1; return 1; }
  if (strcmp(c, "echo") == 0) { *st = b_echo(ac, av); return 1; }
  if (strcmp(c, "cd") == 0) { *st = b_cd(ac, av); return 1; }
  if (strcmp(c, "pwd") == 0) { *st = b_pwd(ac, av); return 1; }
  if (strcmp(c, "set") == 0) { *st = b_set(ac, av); return 1; }
  if (strcmp(c, "shift") == 0) { *st = b_shift(ac, av); return 1; }
  if (strcmp(c, "unset") == 0) { *st = b_unset(ac, av); return 1; }
  if (strcmp(c, "eval") == 0) { *st = b_eval(ac, av); return 1; }
  if (strcmp(c, "read") == 0) { *st = b_read(ac, av); return 1; }
  if (strcmp(c, "exit") == 0) { *st = b_exit(ac, av); return 1; }
  if (strcmp(c, "test") == 0 || strcmp(c, "[") == 0) {
    *st = b_test(ac, av); return 1;
  }
  if (strcmp(c, "local") == 0) {
    int i;
    for (i = 1; i < ac; i = i + 1) {
      char *eq;
      char *name;
      struct var *v;
      eq = strchr(av[i], '=');
      name = eq ? sdup(av[i], (int)(eq - av[i])) : av[i];
      /* 元の値を退避しておく。関数を出るときに戻す */
      if (nloc < NLOC) {
        v = vfind(name);
        locs[nloc].name = sdup0(name);
        locs[nloc].existed = (v != 0);
        locs[nloc].old = (v != 0) ? v->val : "";
        nloc = nloc + 1;
      }
      vset(name, eq ? eq + 1 : "");
    }
    *st = 0;
    return 1;
  }
  return 0;
}

/* ---- 実行 ---- */

/* 簡単コマンド。代入だけの行は変数に入れて終わる */
static int runsimple(int n) {
  int i;
  int st;
  int k;
  char *in;
  char *out;
  int app;
  char *sin;
  char *sout;
  int sapp;
  int nassign;
  int errdup;

  argc_ = 0;
  nassign = 0;
  /* 先頭に並ぶ VAR=val は代入である。展開してから入れる */
  for (i = 0; i < nd[n].wn; i = i + 1) {
    char *w;
    char *eq;
    w = wtab[nd[n].w0 + i];
    eq = strchr(w, '=');
    if (argc_ == 0 && eq != 0 && eq != w && isname1((int)(unsigned char)w[0])) {
      int ok;
      char *q;
      ok = 1;
      for (q = w; q < eq; q = q + 1)
        if (!isnamec((int)(unsigned char)*q)) { ok = 0; break; }
      if (ok) {
        char *name;
        name = sdup(w, (int)(eq - w));
        vset(name, expandone(eq + 1));
        nassign = nassign + 1;
        continue;
      }
    }
    addarg(w);
  }

  /* リダイレクトを解決する */
  in = 0; out = 0; app = 0; errdup = 0;
  (void)errdup;
  for (i = 0; i < nd[n].rn; i = i + 1) {
    struct rdir *r;
    r = &rdt[nd[n].r0 + i];
    if (r->type == R_IN) in = expandone(r->word);
    else if (r->type == R_OUT) { out = expandone(r->word); app = 0; }
    else if (r->type == R_APP) { out = expandone(r->word); app = 1; }
    else if (r->type == R_DUP) {
      /* 2>&1 —— 「fd 2 を fd 1 と同じ先へ」。
       *
       * **カーネルは fd 2 のつなぎ替えを持たない** (spawn が受けるのは
       * in と out の 2 つだけ。docs/stage013-tools.md 3.2)。組込みの
       * 出力は outs() を通るのでここで揃えられるが，外部コマンドの
       * 標準エラーは端末へ出たままになる。
       *
       * configure は cc_msg.txt に警告を溜めて grep するので，この差は
       * 「警告が見つからない = その選択肢を有効にする」方向に効く。
       * 停まらないが結果は変わりうる。塞ぐには spawn の記録を 1 語
       * 伸ばす (err の欄を足す) 必要があり，第 4 部の 3 の課題とする */
      errdup = 1;
    }
    else if (r->type == R_HERE) {
      /* here-doc は一時ファイルへ落として < と同じにする */
      char tn[64];
      int fd;
      char *body;
      tmpname(tn);
      /* 終端子が引用されていなければ本文の $ を展開する (POSIX)。
       * configure の config.mak / config.h はこれで組み立てられる */
      noquote = 1;
      expand1(r->word);
      noquote = 0;
      body = sdup(ebuf, elen);
      fd = open(tn, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd >= 0) {
        write(fd, body, strlen(body));
        close(fd);
      }
      in = sdup0(tn);
    }
  }

  if (argc_ == 0) return 0;                     /* 代入だけ */

  /* 関数か? */
  for (k = 0; k < nfun; k = k + 1) {
    if (strcmp(funs[k].name, argv_[0]) == 0) {
      int sp;
      int i2;
      int savedn;
      char *saved[NPOS];
      int vmark;
      savedn = nposn;
      for (i2 = 0; i2 <= nposn && i2 < NPOS; i2 = i2 + 1) saved[i2] = pos[i2];
      for (i2 = 1; i2 < argc_ && i2 < NPOS; i2 = i2 + 1) pos[i2] = argv_[i2];
      nposn = argc_ - 1;
      vmark = nloc;                             /* local の境界 */
      sp = runtree(funs[k].body);
      /* **local と宣言されたものだけ**を戻す。関数の中の素の代入は
       * 大域に残る (POSIX) */
      while (nloc > vmark) {
        nloc = nloc - 1;
        vset(locs[nloc].name, locs[nloc].old);
      }
      nposn = savedn;
      for (i2 = 0; i2 <= savedn && i2 < NPOS; i2 = i2 + 1) pos[i2] = saved[i2];
      return sp;
    }
  }

  /* 組込みか? リダイレクトは curout / curin を差し替えて効かせる */
  sin = curin; sout = curout; sapp = curapp;
  if (in) curin = in;
  if (out) { curout = out; curapp = app; }
  if (builtin(argc_, argv_, &st)) {
    curin = sin; curout = sout; curapp = sapp;
    return st;
  }
  curin = sin; curout = sout; curapp = sapp;

  /* 外部コマンド。spawn がファイル名で入出力を受ける */
  argv_[argc_] = 0;
  if (in == 0) in = curin;
  if (out == 0 && curout != 0) { out = curout; app = curapp; }
  /* 組込みは stdio を通って出るが，外部コマンドは fd 1 へ直に出る。
   * 流しておかないと順序が入れ替わる */
  fflush(stdout);
  st = spawn(argv_[0], argv_, in, out);
  if (st < 0) {
    fputs(argv_[0], stderr);
    fputs(": not found\n", stderr);
    return 127;
  }
  return st;
}

/* パイプ: 左を一時ファイルへ流し切ってから右を走らせる (9.3) */
static int runpipe(int n) {
  char tn[64];
  char *so;
  char *si;
  int sa;
  int st;
  tmpname(tn);
  so = curout; sa = curapp;
  curout = sdup0(tn); curapp = 0;
  runtree(nd[n].a);
  curout = so; curapp = sa;
  si = curin;
  curin = sdup0(tn);
  st = runtree(nd[n].b);
  curin = si;
  unlink(tn);
  return st;
}

/* 節に付いたリダイレクトを効かせて中を走らせる (簡単コマンド以外) */
static int runrd(int n);

static int runtree(int n) {
  int st;
  if (n < 0 || exiting) return lastst;
  if (nd[n].rn > 0 && nd[n].kind != N_SIMPLE) return runrd(n);
  switch (nd[n].kind) {
  case N_SIMPLE:
    st = runsimple(n);
    lastst = st;
    return st;
  case N_SEQ:
    runtree(nd[n].a);
    if (exiting) return lastst;
    return runtree(nd[n].b);
  case N_AND:
    st = runtree(nd[n].a);
    if (st != 0 || exiting) return st;
    return runtree(nd[n].b);
  case N_OR:
    st = runtree(nd[n].a);
    if (st == 0 || exiting) return st;
    return runtree(nd[n].b);
  case N_NOT:
    st = runtree(nd[n].a);
    lastst = st ? 0 : 1;
    return lastst;
  case N_PIPE:
    st = runpipe(n);
    lastst = st;
    return st;
  case N_GROUP:
    return runtree(nd[n].a);
  case N_IF:
    if (runtree(nd[n].a) == 0) return runtree(nd[n].b);
    return runtree(nd[n].c);
  case N_WHILE:
    st = 0;
    while (!exiting && runtree(nd[n].a) == 0) st = runtree(nd[n].b);
    return st;
  case N_UNTIL:
    st = 0;
    while (!exiting && runtree(nd[n].a) != 0) st = runtree(nd[n].b);
    return st;
  case N_FUNC:
    if (nfun >= NFUN) { fputs("sh2: too many functions\n", stderr); exit(2); }
    funs[nfun].name = wtab[nd[n].w0];
    funs[nfun].body = nd[n].a;
    nfun = nfun + 1;
    return 0;
  case N_FOR: {
    int i;
    int k;
    char *var;
    char *items[NARG];
    int nit;
    var = wtab[nd[n].w0];
    argc_ = 0;
    for (i = 1; i < nd[n].wn; i = i + 1) addarg(wtab[nd[n].w0 + i]);
    nit = argc_;
    for (k = 0; k < nit; k = k + 1) items[k] = argv_[k];
    st = 0;
    for (k = 0; k < nit && !exiting; k = k + 1) {
      vset(var, items[k]);
      st = runtree(nd[n].b);
    }
    return st;
  }
  case N_CASE: {
    int it;
    char *subj;
    subj = expandone(wtab[nd[n].w0]);
    for (it = nd[n].a; it >= 0; it = nd[it].next) {
      int i;
      for (i = 0; i < nd[it].wn; i = i + 1) {
        char *pat;
        pat = expandone(wtab[nd[it].w0 + i]);
        if (patmatch(pat, subj)) return runtree(nd[it].a);
      }
    }
    return 0;
  }
  default:
    return 0;
  }
}

/* 節のリダイレクトを curin / curout へ移してから中を走らせる。
 * 走らせている間だけ効く */
static int runrd(int n) {
  char *sin;
  char *sout;
  int sapp;
  int i;
  int st;
  int rn;
  sin = curin; sout = curout; sapp = curapp;
  rn = nd[n].rn;
  nd[n].rn = 0;                 /* 中で runtree を呼ぶので一度外す */
  for (i = 0; i < rn; i = i + 1) {
    struct rdir *r;
    r = &rdt[nd[n].r0 + i];
    if (r->type == R_IN) curin = expandone(r->word);
    else if (r->type == R_OUT) { curout = expandone(r->word); curapp = 0; }
    else if (r->type == R_APP) { curout = expandone(r->word); curapp = 1; }
  }
  st = runtree(n);
  nd[n].rn = rn;
  curin = sin; curout = sout; curapp = sapp;
  return st;
}

/* 文字列を構文木にして歩く。eval と関数と $() がこれを使う (10.2) */
static int runstr(char *s) {
  char *sip;
  char *stok;
  int stype;
  int snnd;
  int t;
  int st;
  sip = ip; stok = tok; stype = toktype;
  snnd = nnd;
  ip = s;
  lex();
  st = lastst;
  for (;;) {
    skipnl();
    if (toktype == T_EOF) break;
    t = p_list();
    if (t < 0) break;
    st = runtree(t);
    if (exiting) break;
  }
  nnd = snnd;                   /* 使った節を返す (関数の本体は残らない) */
  ip = sip; tok = stok; toktype = stype;
  return st;
}

int main(int argc, char **argv) {
  int fd;
  int n;
  int i;
  int t;

  pos[0] = (argc > 0) ? argv[0] : "sh2";
  nposn = 0;

  if (argc > 1) {
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
      fputs("sh2: cannot open ", stderr);
      fputs(argv[1], stderr);
      fputs("\n", stderr);
      return 1;
    }
    pos[0] = argv[1];
    for (i = 2; i < argc && i - 1 < NPOS; i = i + 1) {
      pos[i - 1] = argv[i];
      nposn = i - 1;
    }
  } else {
    fd = 0;
  }

  n = 0;
  for (;;) {
    int r;
    if (n >= NSRC - 1) break;
    r = read(fd, src + n, (size_t)(NSRC - 1 - n));
    if (r <= 0) break;
    n = n + r;
  }
  if (fd != 0) close(fd);
  src[n] = 0;

  ip = src;
  lex();
  for (;;) {
    skipnl();
    if (toktype == T_EOF) break;
    t = p_list();
    if (t < 0) break;
    runtree(t);
    if (exiting) break;
  }
  return exiting ? exitst : lastst;
}
