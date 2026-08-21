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
  return out;
}

/* 次の語 / 記号を取る */
static void lex(void) {
  char *st;
  int q;
  int c;

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

/* 語がそのまま予約語になる位置かどうかは呼び手が決める */
static int isreserved(char *s) {
  return strcmp(s, "if") == 0 || strcmp(s, "then") == 0
      || strcmp(s, "elif") == 0 || strcmp(s, "else") == 0
      || strcmp(s, "fi") == 0 || strcmp(s, "while") == 0
      || strcmp(s, "until") == 0 || strcmp(s, "do") == 0
      || strcmp(s, "done") == 0 || strcmp(s, "for") == 0
      || strcmp(s, "in") == 0 || strcmp(s, "case") == 0
      || strcmp(s, "esac") == 0 || strcmp(s, "}") == 0;
}

static void expect(char *s) {
  if (!iskw(s)) {
    fprintf(stderr, "sh2: syntax error: expected %s\n", s);
    exit(2);
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
    if (fd >= 0) { fputs("sh2: syntax error near fd\n", stderr); exit(2); }
    return 0;
  }
  lex();
  if (toktype != T_WORD) { fputs("sh2: syntax error: redirect\n", stderr); exit(2); }
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
  if (toktype != T_WORD) { fputs("sh2: syntax error: for\n", stderr); exit(2); }
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
  if (toktype != T_WORD) { fputs("sh2: syntax error: case\n", stderr); exit(2); }
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
    if (toktype != T_RP) { fputs("sh2: syntax error: case pattern\n", stderr); exit(2); }
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

static int p_command(void) {
  int n;
  int rn;

  skipnl();
  if (toktype == T_EOF) return -1;
  if (iskw("if")) return p_if();
  if (iskw("while")) return p_while(N_WHILE);
  if (iskw("until")) return p_while(N_UNTIL);
  if (iskw("for")) return p_for();
  if (iskw("case")) return p_case();
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
    if (toktype != T_RP) { fputs("sh2: syntax error: )\n", stderr); exit(2); }
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
    if (toktype == T_WORD && isreserved(tok)) break;
    if (toktype == T_RP || toktype == T_DSEMI) break;
    n = newnode(N_SEQ);
    nd[n].a = l;
    nd[n].b = p_andor();
    if (nd[n].b < 0) { nnd = nnd - 1; break; }
    l = n;
  }
  return l;
}
