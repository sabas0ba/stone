/* mk17.c --- make の第 17 世代 (Stage 17 第 3 部の 1)
 *
 * 設計は docs/stage017-cc.md 9 章。Stage 13 の mk (240 行，変数すら
 * 無い) を出発点にせず，書き直したものである。
 *
 *   mk [-f 記述] [-n] [-s] [目標...]
 *
 * 持っているもの:
 *
 *   変数        NAME = v (遅延展開) / := (即時) / += / ?=
 *               左辺も展開する (TCCDEFS_H$(subst ...) = ... のため)
 *   展開        $(V) ${V} $V $$。**名前を先に展開してから引く**ので
 *               $($T_FILES) が通る (9.2)
 *   条件        ifeq ifneq ifdef ifndef else endif。入れ子になる
 *   取り込み    include / -include
 *   規則        目標...: 依存... と，行頭が TAB の命令行
 *   型規則      %.o : %.c
 *   自動変数    $@ $< $^ $*
 *   .PHONY      受ける (9.4 のとおり判定をしないので働きは無い)
 *   命令の頭    @ (表示しない) / - (失敗を無視)
 *
 * ---- 設計の要 ----
 *
 * **命令行は自分で切らず sh2 へ渡す** (9.3)。tcc の Makefile の命令は
 * && や || や $(...) の入った合成命令を含むので，切り直すのはシェルを
 * 二度書くことになる。sh2 に -c は無いので一時ファイルへ書いて渡す。
 *
 * **作り直しの判定はしない。目標に規則があれば必ず作る** (9.4)。
 * sfs2 の項目が時刻を持たないためである。遅いが嘘はつかない。
 * この一点は -h でも出す —— 黙って「作らなかった」ことにするのが
 * いちばん悪い。
 *
 * **関数 ($(subst ...) など) は第 3 部の 2 である。** ここでは
 * 実装せず，**見つけたら落とす**。変数名として引いて空に展開すると，
 * 我々が何度も踏んだ「動くように見えて意味が違う」型になる。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define NVAR   512              /* 変数の数 */
#define NRULE  256              /* 規則の数 */
#define NLIST  4096             /* 依存・命令の総数 */
#define NTXT   262144           /* 記述の置き場 */
#define NEXP   65536            /* 展開の器 */
#define NCOND  32               /* 条件の入れ子 */
#define NGOAL  32               /* 目標の数 */
#define NSTK   64               /* 作る途中の深さ */

#define TMP "_mk.sh"

/* 変数の種類 */
#define V_LAZY 0                /* = 引くたびに展開する */
#define V_SIMP 1                /* := 代入のときに展開済み */

/* 大きな作業領域は大域に置く (docs/dev-notes.md 4 章) */
char txt[NTXT];                 /* 記述の実体 (行ごとに NUL) */
int txtn;

char *vname[NVAR];
char *vval[NVAR];
int vkind[NVAR];
int nvar;

char *rtgt[NRULE];              /* 目標 (% を含みうる) */
int rdep0[NRULE];               /* 依存の並びの開始 (list 内) */
int rdepn[NRULE];
int rcmd0[NRULE];               /* 命令の並びの開始 (list 内) */
int rcmdn[NRULE];
int nrule;

char *list[NLIST];              /* 依存の名前と命令行を混ぜて積む */
int nlist;

char *phony[NRULE];             /* .PHONY に並んだ名前 */
int nphony;

char *goals[NGOAL];
int ngoal;

char *making[NSTK];             /* 作っている途中の目標 (輪の検出) */
int nmaking;
char *made[NRULE];              /* 作り終えた目標 (1 度だけにする) */
int nmade;

/* 自動変数。規則を実行する間だけ立つ */
char *a_tgt;
char *a_first;                  /* $< */
char *a_all;                    /* $^ (空白区切りに組み立てたもの) */
char *a_stem;                   /* $* */
char allbuf[NEXP];
char stembuf[256];

int dryrun;                     /* -n */
int silent;                     /* -s */

static void die(char *m, char *a) {
  fputs("mk: ", stderr);
  fputs(m, stderr);
  if (a) {
    fputs(": ", stderr);
    fputs(a, stderr);
  }
  fputs("\n", stderr);
  exit(2);
}

/* txt へ写して不変の文字列にする。**dup という名前は使わない**
 * (POSIX の dup(2) と衝突し，ホストで取り込むとき型が食い違う) */
static char *keep(char *s, int n) {
  char *p;
  if (txtn + n + 1 > NTXT) die("out of text space", 0);
  p = txt + txtn;
  memcpy(p, s, n);
  p[n] = 0;
  txtn = txtn + n + 1;
  return p;
}

static char *keeps(char *s) { return keep(s, (int)strlen(s)); }

static int isblank_(int c) { return c == ' ' || c == '\t'; }

static char *skipb(char *s) {
  while (isblank_(*s)) s = s + 1;
  return s;
}

/* 末尾の空白を削る (その場で書き換える) */
static void rstrip(char *s) {
  int n;
  n = (int)strlen(s);
  while (n > 0 && isblank_(s[n - 1])) n = n - 1;
  s[n] = 0;
}

/* ---- 変数 ---- */

static int vfind(char *n) {
  int i;
  for (i = 0; i < nvar; i = i + 1)
    if (strcmp(vname[i], n) == 0) return i;
  return -1;
}

static void vset(char *n, char *v, int kind) {
  int i;
  i = vfind(n);
  if (i < 0) {
    if (nvar >= NVAR) die("too many variables", n);
    i = nvar;
    nvar = nvar + 1;
    vname[i] = keeps(n);
  }
  vval[i] = keeps(v);
  vkind[i] = kind;
}

static void expand(char *s, char *out, int cap);

/* 変数の値を引く。遅延なら引くたびに展開する */
static void vget(char *n, char *out, int cap) {
  int i;
  i = vfind(n);
  if (i < 0) {
    out[0] = 0;
    return;
  }
  if (vkind[i] == V_SIMP) {
    if ((int)strlen(vval[i]) >= cap) die("value too long", n);
    strcpy(out, vval[i]);
  } else {
    expand(vval[i], out, cap);
  }
}

/* $(...) / ${...} の中身を閉じ括弧まで取る。開き括弧の次を指して
 * 呼び，閉じ括弧の次を返す。入れ子を数える */
static char *inner(char *s, int open, int close, char *out, int cap) {
  int depth;
  int n;
  depth = 1;
  n = 0;
  while (*s) {
    if (*s == open) {
      depth = depth + 1;
    } else if (*s == close) {
      depth = depth - 1;
      if (depth == 0) {
        out[n] = 0;
        return s + 1;
      }
    }
    if (n + 1 >= cap) die("expansion too long", 0);
    out[n] = *s;
    n = n + 1;
    s = s + 1;
  }
  die("unterminated reference", out);
  return s;
}

/* $(...) の中身が関数の呼び出しか。深さ 0 の空白があればそう見る。
 * **第 3 部の 1 では実装しないので，見分けて落とすためだけに要る** */
static int looksfn(char *s) {
  int depth;
  depth = 0;
  while (*s) {
    if (*s == '(' || *s == '{') depth = depth + 1;
    else if (*s == ')' || *s == '}') depth = depth - 1;
    else if (depth == 0 && isblank_(*s)) return 1;
    s = s + 1;
  }
  return 0;
}

/* 自動変数か。1 文字を見て，該当すれば値を out へ入れて 1 を返す */
static int autovar(int c, char *out, int cap) {
  char *v;
  v = 0;
  if (c == '@') v = a_tgt;
  else if (c == '<') v = a_first;
  else if (c == '^') v = a_all;
  else if (c == '*') v = a_stem;
  if (v == 0) return 0;
  if ((int)strlen(v) >= cap) die("automatic variable too long", 0);
  strcpy(out, v);
  return 1;
}

/* s を展開して out へ。cap は out の容量 */
static void expand(char *s, char *out, int cap) {
  int n;
  char nm[1024];                /* 参照の中身 (展開前) */
  char nm2[1024];               /* 同 (展開後 = 引く名前) */
  char val[NEXP];
  n = 0;
  while (*s) {
    if (*s != '$') {
      if (n + 1 >= cap) die("expansion too long", 0);
      out[n] = *s;
      n = n + 1;
      s = s + 1;
      continue;
    }
    s = s + 1;
    if (*s == 0) break;
    if (*s == '$') {
      if (n + 1 >= cap) die("expansion too long", 0);
      out[n] = '$';
      n = n + 1;
      s = s + 1;
      continue;
    }
    if (*s == '(' || *s == '{') {
      int open;
      int close;
      open = *s;
      close = (open == '(') ? ')' : '}';
      s = inner(s + 1, open, close, nm, (int)sizeof nm);
      if (looksfn(nm)) {
        /* **黙って空に展開しない** (9.5 / 第 3 部の 2) */
        die("functions are not implemented yet (part 2)", nm);
      }
      /* **名前を先に展開する。** $($T_FILES) が通るのはここである */
      expand(nm, nm2, (int)sizeof nm2);
      vget(nm2, val, (int)sizeof val);
    } else {
      /* $X。1 文字。自動変数でなければ 1 文字の変数名 */
      if (!autovar(*s, val, (int)sizeof val)) {
        nm2[0] = *s;
        nm2[1] = 0;
        vget(nm2, val, (int)sizeof val);
      }
      s = s + 1;
    }
    if (n + (int)strlen(val) >= cap) die("expansion too long", 0);
    strcpy(out + n, val);
    n = n + (int)strlen(val);
  }
  out[n] = 0;
}

/* ---- 記述の読み込み ---- */

/* 1 つのファイルを読んで buf へ。返り値は長さ，開けなければ -1 */
static int slurp(char *path, char *buf, int cap) {
  int fd;
  int n;
  int r;
  fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  n = 0;
  for (;;) {
    if (n >= cap - 1) die("makefile too big", path);
    r = read(fd, buf + n, cap - 1 - n);
    if (r <= 0) break;
    n = n + r;
  }
  close(fd);
  buf[n] = 0;
  return n;
}

/* 条件の状態 */
int cond[NCOND];                /* 1 = 今の枝が生きている */
int condseen[NCOND];            /* 1 = すでに生きた枝があった */
int ncond;

static int active(void) {
  int i;
  for (i = 0; i < ncond; i = i + 1)
    if (!cond[i]) return 0;
  return 1;
}

/* ifeq (a,b) / ifeq "a" "b" の 2 つを剥がして比べる */
static int cmpargs(char *s) {
  char e[NEXP];
  char *a;
  char *b;
  char *p;
  expand(s, e, (int)sizeof e);
  p = skipb(e);
  if (*p == '(') {
    a = p + 1;
    b = strchr(a, ',');
    if (b == 0) die("bad ifeq", s);
    *b = 0;
    b = b + 1;
    p = b + strlen(b);
    while (p > b && *(p - 1) != ')') p = p - 1;
    if (p > b) *(p - 1) = 0;
    rstrip(a);
    a = skipb(a);
    rstrip(b);
    b = skipb(b);
    return strcmp(a, b) == 0;
  }
  /* 引用符の形 */
  if (*p == '"' || *p == '\'') {
    int q;
    q = *p;
    a = p + 1;
    p = strchr(a, q);
    if (p == 0) die("bad ifeq", s);
    *p = 0;
    p = skipb(p + 1);
    if (*p != '"' && *p != '\'') die("bad ifeq", s);
    q = *p;
    b = p + 1;
    p = strchr(b, q);
    if (p == 0) die("bad ifeq", s);
    *p = 0;
    return strcmp(a, b) == 0;
  }
  die("bad ifeq", s);
  return 0;
}

static void parse(char *path, int required);

/* 目標行を 1 つ足す */
static int addrule(char *tgt) {
  if (nrule >= NRULE) die("too many rules", tgt);
  rtgt[nrule] = keeps(tgt);
  rdep0[nrule] = nlist;
  rdepn[nrule] = 0;
  rcmd0[nrule] = 0;
  rcmdn[nrule] = 0;
  nrule = nrule + 1;
  return nrule - 1;
}

static void addlist(char *s) {
  if (nlist >= NLIST) die("too many prerequisites/commands", s);
  list[nlist] = keeps(s);
  nlist = nlist + 1;
}

/* 行の並びを読んで表を作る */
static void parselines(char *buf, char *path) {
  char *p;
  char line[8192];
  int cur;                      /* 直前の規則 (命令行の行き先)。-1 = 無し */
  cur = -1;
  p = buf;
  while (*p) {
    char *e;
    int len;
    int istab;
    /* 1 行取り出す。行末の \ は次の行と繋ぐ */
    len = 0;
    istab = (*p == '\t');
    for (;;) {
      e = strchr(p, '\n');
      if (e == 0) e = p + strlen(p);
      if (len + (e - p) + 2 >= (int)sizeof line) die("line too long", path);
      memcpy(line + len, p, (size_t)(e - p));
      len = len + (int)(e - p);
      line[len] = 0;
      p = (*e == 0) ? e : e + 1;
      if (len > 0 && line[len - 1] == '\\') {
        /* 継続。\ を空白 1 つに置き換え，次の行の頭の空白は畳む */
        line[len - 1] = ' ';
        len = len - 1;
        line[len] = 0;
        strcat(line, " ");
        len = len + 1;
        p = skipb(p);
        continue;
      }
      break;
    }

    /* 注釈を落とす。**命令行では落とさない** (シェルへ渡すため) */
    if (!istab) {
      char *h;
      h = line;
      for (;;) {
        h = strchr(h, '#');
        if (h == 0) break;
        if (h > line && *(h - 1) == '\\') {
          h = h + 1;
          continue;
        }
        *h = 0;
        break;
      }
      rstrip(line);
    }

    /* ---- 条件 ---- */
    {
      char *w;
      w = skipb(line);
      if (strncmp(w, "ifeq", 4) == 0 || strncmp(w, "ifneq", 5) == 0
          || strncmp(w, "ifdef", 5) == 0 || strncmp(w, "ifndef", 6) == 0) {
        int neg;
        int def;
        int r;
        char *arg;
        def = (strncmp(w, "ifdef", 5) == 0 || strncmp(w, "ifndef", 6) == 0);
        neg = (strncmp(w, "ifneq", 5) == 0 || strncmp(w, "ifndef", 6) == 0);
        arg = w;
        while (*arg && !isblank_(*arg)) arg = arg + 1;
        arg = skipb(arg);
        if (ncond >= NCOND) die("conditionals nested too deep", path);
        if (!active()) {
          /* 外側が死んでいる。中身は見ない */
          cond[ncond] = 0;
          condseen[ncond] = 1;
          ncond = ncond + 1;
          continue;
        }
        if (def) {
          char nm[1024];
          int i;
          expand(arg, nm, (int)sizeof nm);
          rstrip(nm);
          i = vfind(nm);
          r = (i >= 0 && vval[i][0] != 0);
        } else {
          r = cmpargs(arg);
        }
        if (neg) r = !r;
        cond[ncond] = r;
        condseen[ncond] = r;
        ncond = ncond + 1;
        continue;
      }
      if (strcmp(w, "else") == 0 || strncmp(w, "else ", 5) == 0) {
        if (ncond == 0) die("else without if", path);
        /* else if... の形は取らない (tcc の Makefile には無い) */
        if (skipb(w + 4)[0] != 0)
          die("`else if' is not implemented", w);
        cond[ncond - 1] = condseen[ncond - 1] ? 0 : 1;
        if (cond[ncond - 1]) condseen[ncond - 1] = 1;
        continue;
      }
      if (strcmp(w, "endif") == 0) {
        if (ncond == 0) die("endif without if", path);
        ncond = ncond - 1;
        continue;
      }
    }
    if (!active()) continue;

    /* ---- 命令行 ---- */
    if (istab) {
      if (cur < 0) die("recipe line without a rule", line);
      if (rcmdn[cur] == 0) rcmd0[cur] = nlist;
      addlist(line + 1);        /* 頭の TAB を落とす */
      rcmdn[cur] = rcmdn[cur] + 1;
      continue;
    }

    if (skipb(line)[0] == 0) { cur = -1; continue; }

    /* ---- include ---- */
    {
      char *w;
      int req;
      w = skipb(line);
      req = 1;
      if (*w == '-') { req = 0; w = w + 1; }
      if (strncmp(w, "include", 7) == 0 && isblank_(w[7])) {
        char e[NEXP];
        char *q;
        expand(skipb(w + 7), e, (int)sizeof e);
        q = skipb(e);
        while (*q) {
          char one[512];
          int k;
          k = 0;
          while (*q && !isblank_(*q)) {
            if (k + 1 >= (int)sizeof one) die("include path too long", e);
            one[k] = *q;
            k = k + 1;
            q = q + 1;
          }
          one[k] = 0;
          q = skipb(q);
          if (k) parse(one, req);
        }
        cur = -1;
        continue;
      }
    }

    /* ---- 代入か規則か ----
     *
     * 左から 1 文字ずつ見て，先に来たほうで決める。**':' を先に
     * 探すと CFLAGS = a:b が規則に見える** */
    {
      char *q;
      char *eq;
      char *colon;
      eq = 0;
      colon = 0;
      for (q = line; *q; q = q + 1) {
        if (*q == '=') {
          if (q > line && (*(q - 1) == ':' || *(q - 1) == '+'
                           || *(q - 1) == '?')) eq = q - 1;
          else eq = q;
          break;
        }
        if (*q == ':') {
          /* := は代入。それ以外の ':' は規則の印 */
          if (*(q + 1) == '=') { eq = q; break; }
          colon = q;
          break;
        }
      }

      if (eq) {
        char lhs[1024];
        char rhs[NEXP];
        char val[NEXP];
        int op;
        char *v;
        op = *eq;                     /* ':' '+' '?' か '=' */
        v = (op == '=') ? eq + 1 : eq + 2;
        *eq = 0;
        rstrip(line);
        /* **左辺も展開する** (9.2) */
        expand(skipb(line), lhs, (int)sizeof lhs);
        rstrip(lhs);
        v = skipb(v);
        if (op == ':') {
          expand(v, val, (int)sizeof val);
          vset(lhs, val, V_SIMP);
        } else if (op == '+') {
          int i;
          i = vfind(lhs);
          if (i < 0) {
            vset(lhs, v, V_LAZY);
          } else if (vkind[i] == V_SIMP) {
            expand(v, val, (int)sizeof val);
            if ((int)strlen(vval[i]) + (int)strlen(val) + 2 >= NEXP)
              die("value too long", lhs);
            strcpy(rhs, vval[i]);
            strcat(rhs, " ");
            strcat(rhs, val);
            vset(lhs, rhs, V_SIMP);
          } else {
            if ((int)strlen(vval[i]) + (int)strlen(v) + 2 >= NEXP)
              die("value too long", lhs);
            strcpy(rhs, vval[i]);
            strcat(rhs, " ");
            strcat(rhs, v);
            vset(lhs, rhs, V_LAZY);
          }
        } else if (op == '?') {
          if (vfind(lhs) < 0) vset(lhs, v, V_LAZY);
        } else {
          vset(lhs, v, V_LAZY);
        }
        cur = -1;
        continue;
      }

      if (colon) {
        char tg[NEXP];
        char dp[NEXP];
        char *q2;
        int first;
        *colon = 0;
        expand(skipb(line), tg, (int)sizeof tg);
        expand(skipb(colon + 1), dp, (int)sizeof dp);
        rstrip(tg);
        rstrip(dp);
        /* 目標が複数並ぶことがある。それぞれに同じ規則を立てる */
        first = -1;
        q2 = skipb(tg);
        while (*q2) {
          char one[512];
          int k;
          int ri;
          k = 0;
          while (*q2 && !isblank_(*q2)) {
            if (k + 1 >= (int)sizeof one) die("target too long", tg);
            one[k] = *q2;
            k = k + 1;
            q2 = q2 + 1;
          }
          one[k] = 0;
          q2 = skipb(q2);
          if (k == 0) break;
          if (strcmp(one, ".PHONY") == 0) {
            char *d;
            d = skipb(dp);
            while (*d) {
              char w2[256];
              int j;
              j = 0;
              while (*d && !isblank_(*d)) {
                if (j + 1 >= (int)sizeof w2) die("name too long", dp);
                w2[j] = *d;
                j = j + 1;
                d = d + 1;
              }
              w2[j] = 0;
              d = skipb(d);
              if (j && nphony < NRULE) {
                phony[nphony] = keeps(w2);
                nphony = nphony + 1;
              }
            }
            first = -1;
            break;
          }
          ri = addrule(one);
          if (first < 0) first = ri;
          {
            char *d;
            d = skipb(dp);
            while (*d) {
              char w2[512];
              int j;
              j = 0;
              while (*d && !isblank_(*d)) {
                if (j + 1 >= (int)sizeof w2) die("name too long", dp);
                w2[j] = *d;
                j = j + 1;
                d = d + 1;
              }
              w2[j] = 0;
              d = skipb(d);
              if (j) {
                addlist(w2);
                rdepn[ri] = rdepn[ri] + 1;
              }
            }
          }
        }
        cur = first;
        continue;
      }
    }
    die("cannot parse line", line);
  }
}

/* 1 つの記述を読んで表に足す。required が 0 なら無くてもよい */
static void parse(char *path, int required) {
  static char buf[NTXT];
  if (slurp(path, buf, (int)sizeof buf) < 0) {
    if (required) die("cannot open", path);
    return;
  }
  parselines(buf, path);
}

/* ---- 作る ---- */

static int isphony(char *t) {
  int i;
  for (i = 0; i < nphony; i = i + 1)
    if (strcmp(phony[i], t) == 0) return 1;
  return 0;
}

static int exists(char *p) {
  int fd;
  fd = open(p, O_RDONLY);
  if (fd < 0) return 0;
  close(fd);
  return 1;
}

/* 型規則 %.o : %.c の目標 pat が t に合うか。合えば語幹を stem へ */
static int patmatch(char *pat, char *t, char *stem, int cap) {
  char *pc;
  int pre;
  int suf;
  int tn;
  pc = strchr(pat, '%');
  if (pc == 0) return 0;
  pre = (int)(pc - pat);
  suf = (int)strlen(pc + 1);
  tn = (int)strlen(t);
  if (tn < pre + suf) return 0;
  if (strncmp(pat, t, (size_t)pre) != 0) return 0;
  if (strcmp(t + tn - suf, pc + 1) != 0) return 0;
  if (tn - pre - suf + 1 > cap) return 0;
  memcpy(stem, t + pre, (size_t)(tn - pre - suf));
  stem[tn - pre - suf] = 0;
  return 1;
}

/* % を stem で置き換える */
static void patsub(char *pat, char *stem, char *out, int cap) {
  char *pc;
  int n;
  pc = strchr(pat, '%');
  if (pc == 0) {
    if ((int)strlen(pat) >= cap) die("name too long", pat);
    strcpy(out, pat);
    return;
  }
  n = (int)(pc - pat);
  if (n + (int)strlen(stem) + (int)strlen(pc + 1) >= cap)
    die("name too long", pat);
  memcpy(out, pat, (size_t)n);
  strcpy(out + n, stem);
  strcat(out, pc + 1);
}

static void make(char *t);

/* 命令行を 1 つ走らせる。**sh2 へ渡す** (9.3) */
static void runcmd(char *raw) {
  char e[NEXP];
  char *c;
  int quiet;
  int ignore;
  int fd;
  int st;
  char *av[3];

  expand(raw, e, (int)sizeof e);
  c = skipb(e);
  quiet = 0;
  ignore = 0;
  for (;;) {
    if (*c == '@') { quiet = 1; c = c + 1; continue; }
    if (*c == '-') { ignore = 1; c = c + 1; continue; }
    if (*c == '+') { c = c + 1; continue; }   /* 再帰の印。素通し */
    break;
  }
  c = skipb(c);
  if (*c == 0) return;
  if (!quiet && !silent) {
    fputs(c, stdout);
    fputs("\n", stdout);
  }
  if (dryrun) return;

  fd = open(TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TMP);
  write(fd, c, (int)strlen(c));
  write(fd, "\n", 1);
  close(fd);

  av[0] = "/bin/sh2";
  av[1] = TMP;
  av[2] = 0;
  st = spawn2("/bin/sh2", av, 0, 0, 0);
  if (st != 0 && !ignore) {
    char n[32];
    sprintf(n, "%d", st);
    fputs("mk: *** [", stderr);
    fputs(a_tgt ? a_tgt : "?", stderr);
    fputs("] error ", stderr);
    fputs(n, stderr);
    fputs("\n", stderr);
    exit(2);
  }
}

/* 規則 ri を目標 t (語幹 stem) として実行する */
static void fire(int ri, char *t, char *stem) {
  int i;
  char dep[512];
  char *savet;
  char *savef;
  char *savea;
  char *saves;
  int n;

  /* 依存を先に作る。**自動変数を立てる前に**やる (入れ子で潰れる) */
  for (i = 0; i < rdepn[ri]; i = i + 1) {
    patsub(list[rdep0[ri] + i], stem, dep, (int)sizeof dep);
    make(dep);
  }

  savet = a_tgt;
  savef = a_first;
  savea = a_all;
  saves = a_stem;

  a_tgt = t;
  n = 0;
  allbuf[0] = 0;
  a_first = "";
  for (i = 0; i < rdepn[ri]; i = i + 1) {
    patsub(list[rdep0[ri] + i], stem, dep, (int)sizeof dep);
    if (i == 0) a_first = keeps(dep);
    if (n + (int)strlen(dep) + 2 >= (int)sizeof allbuf)
      die("prerequisite list too long", t);
    if (n) { allbuf[n] = ' '; n = n + 1; }
    strcpy(allbuf + n, dep);
    n = n + (int)strlen(dep);
  }
  a_all = allbuf;
  strcpy(stembuf, stem);
  a_stem = stembuf;

  for (i = 0; i < rcmdn[ri]; i = i + 1) runcmd(list[rcmd0[ri] + i]);

  a_tgt = savet;
  a_first = savef;
  a_all = savea;
  a_stem = saves;
}

static void make(char *t) {
  int i;
  int ri;
  char stem[256];

  for (i = 0; i < nmade; i = i + 1)
    if (strcmp(made[i], t) == 0) return;
  for (i = 0; i < nmaking; i = i + 1)
    if (strcmp(making[i], t) == 0) die("circular dependency", t);
  if (nmaking >= NSTK) die("dependencies nested too deep", t);
  making[nmaking] = t;
  nmaking = nmaking + 1;

  /* まず名前がそのまま合う規則 */
  ri = -1;
  stem[0] = 0;
  for (i = 0; i < nrule; i = i + 1)
    if (strchr(rtgt[i], '%') == 0 && strcmp(rtgt[i], t) == 0) { ri = i; break; }

  /* 無ければ型規則。**依存が作れるものを選ぶ**ところまではやらない
   * (最初に合ったものを使う)。tcc の Makefile はこの範囲に収まる */
  if (ri < 0) {
    for (i = 0; i < nrule; i = i + 1) {
      if (strchr(rtgt[i], '%') == 0) continue;
      if (patmatch(rtgt[i], t, stem, (int)sizeof stem)) { ri = i; break; }
    }
  }

  if (ri < 0) {
    if (!exists(t) && !isphony(t))
      die("no rule to make target", t);
  } else {
    fire(ri, t, stem);
  }

  nmaking = nmaking - 1;
  if (nmade < NRULE) {
    made[nmade] = keeps(t);
    nmade = nmade + 1;
  }
}

static void usage(void) {
  fputs("usage: mk [-f makefile] [-n] [-s] [target...]\n", stderr);
  fputs("  **作り直しの判定はしない。規則のある目標は必ず作る。**\n",
        stderr);
  fputs("  (sfs2 の項目が時刻を持たないため。"
        "docs/stage017-cc.md 9.4)\n", stderr);
}

int main(int argc, char **argv) {
  int i;
  char *mf;
  mf = "Makefile";
  for (i = 1; i < argc; i = i + 1) {
    char *a;
    a = argv[i];
    if (strcmp(a, "-f") == 0) {
      i = i + 1;
      if (i >= argc) { usage(); return 2; }
      mf = argv[i];
      continue;
    }
    if (strcmp(a, "-n") == 0) { dryrun = 1; continue; }
    if (strcmp(a, "-s") == 0) { silent = 1; continue; }
    if (strcmp(a, "-h") == 0) { usage(); return 0; }
    if (a[0] == '-' && a[1] != 0) { usage(); return 2; }
    if (ngoal < NGOAL) { goals[ngoal] = a; ngoal = ngoal + 1; }
  }

  a_tgt = "";
  a_first = "";
  a_all = "";
  a_stem = "";
  parse(mf, 1);
  if (ncond != 0) die("missing endif in", mf);
  if (nrule == 0) die("no rules in", mf);

  if (ngoal == 0) {
    /* 既定は最初のふつうの目標 */
    for (i = 0; i < nrule; i = i + 1)
      if (strchr(rtgt[i], '%') == 0 && rtgt[i][0] != '.') {
        goals[0] = rtgt[i];
        ngoal = 1;
        break;
      }
    if (ngoal == 0) die("no default goal in", mf);
  }

  for (i = 0; i < ngoal; i = i + 1) make(goals[i]);
  return 0;
}
