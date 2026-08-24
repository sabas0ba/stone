/* mk19.c --- make の第 19 世代 (Stage 17 第 3 部の 2)
 *
 * mk18 の写しに**関数**を入れたものである
 * (docs/stage017-cc.md 9.1 で数えた 12 種)。
 *
 *   $(call f,a,b)          利用者定義の関数。$(1) $(2) で受ける
 *   $(if c,then,else)      c が空でなければ then
 *   $(or a,b,...)          最初の空でないもの
 *   $(foreach v,list,txt)  list の語ごとに v を立てて txt を展開
 *   $(filter p,list)       $(filter-out p,list)
 *   $(findstring a,b)      b に a があれば a
 *   $(subst a,b,t)         $(patsubst p,r,list)
 *   $(addprefix p,list)    $(addsuffix s,list)
 *   $(firstword list)      $(wildcard p)      $(shell cmd)
 *
 * **評価の順が種類で違う。** ほとんどの関数は引数を先に展開してから
 * 呼ぶが，`if` / `or` / `foreach` / `call` は**呼んでから中で展開する**
 * (でないと `$(if $(X),$(BOOM))` の BOOM が常に展開されてしまう)。
 *
 * **知らない関数は空に展開せず落とす。** mk17 からの決まりで，
 * 変数名として引いて空にすると「動くように見えて意味が違う」型になる。
 *
 * 以下は mk18 からの引き継ぎ。
 *
 * mk17 の写しに**作り直しの判定**を入れたものである
 * (docs/stage017-cc.md 11 章)。
 *
 * mk17 は「規則のある目標は必ず作る」だった。sfs2 が時刻を持たず，
 * 古さを判定できなかったからである (9.4)。sfs3 が時刻を持ったので，
 * **依存より古いときだけ作る**ようになった。
 *
 *   目標が無い                      -> 作る
 *   .PHONY の目標                   -> 必ず作る
 *   依存のどれかが目標より新しい    -> 作る
 *   それ以外                        -> 作らない
 *
 * 時刻は epoch からのナノ秒を u32 2 本で持つ。比べるのは libc19 の
 * newer() で，**上位語から見る** (11.2)。
 *
 * 目標が最後まで作られなかったときは，最後に `mk: '<目標>' is up to
 * date.` と出す。本物の make と同じ形である。**黙って終わると
 * 「作ったのか作らなかったのか」が呼び手に判らない。**
 *
 * 以下は mk17 からの引き継ぎ。
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
 * **関数 ($(subst ...) など) は第 3 部の 2 である。** ここでは
 * 実装せず，**見つけたら落とす**。変数名として引いて空に展開すると，
 * 我々が何度も踏んだ「動くように見えて意味が違う」型になる。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define NVAR   512              /* 変数の数 */
#define NRULE  256              /* 規則の数 */
#define NLIST  4096             /* 依存・命令の総数 */
#define NTXT   262144           /* 記述の置き場 */
#define NEXP   16384            /* 展開の器 (下の註を見よ) */
#define NCOND  32               /* 条件の入れ子 */
#define NGOAL  32               /* 目標の数 */
#define NSTK   64               /* 作る途中の深さ */

/* NEXP を 16384 にしてあるのは**器の都合**である。cc15p は 1 つの
 * 関数のフレームがおよそ 32 KB を超えると終了コード 6 で落ちる。
 * parselines は tg[NEXP] と dp[NEXP] を並べて持つので，NEXP を
 * 32768 にすると 64 KB になって通らない (実測: 16384 通る /
 * 32768 落ちる)。
 *
 * **溢れたときは黙って切らず die する。** 16384 は tcc の Makefile の
 * どの展開よりも十分に大きいが，足りなくなったら分かるようにしておく */

#define TMP "_mk.sh"
#define SHOUT "_mk.out"        /* $(shell ...) の受け皿 */

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
int always;                     /* -B (古さを見ずに必ず作る) */

/* この走りで命令を 1 つでも走らせたか。
 *
 * **「作ると決めたか」ではなく「命令が走ったか」で数える。** `all: prog`
 * のような命令を持たない目標は，作ると決めても何もしない。決めたほうで
 * 数えると，何もしなかった走りが「作った」ことになる */
int built;

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
static int exists(char *p);
/* spawn2 は libc19 の <unistd.h> が宣言する (ホストの殻では殻が宣言する)。
 * **ここで宣言しない** —— static を付けると実体の無い静的関数になり，
 * 付けないと殻の static と食い違う */

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

/* $(...) の中身が関数の呼び出しか。深さ 0 の空白があればそう見る */
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

/* ---- 関数 (第 3 部の 2) ---- */

#define NARG 12                 /* 関数の引数の上限 ($(call) の分を含む) */

/* 深さ 0 の ',' で切って argv[] へ。返り値は個数。
 * **入れ子の $(...) の中の ',' では切らない** —— 切ると
 * $(if $(filter a,b),y) のような入れ子が壊れる */
static int splitargs(char *s, char **argv, int max) {
  int n;
  int depth;
  n = 0;
  argv[0] = s;
  depth = 0;
  while (*s) {
    if (*s == '(' || *s == '{') depth = depth + 1;
    else if (*s == ')' || *s == '}') depth = depth - 1;
    else if (*s == ',' && depth == 0 && n + 1 < max) {
      *s = 0;
      n = n + 1;
      argv[n] = s + 1;
    }
    s = s + 1;
  }
  return n + 1;
}

/* 語を 1 つ取り出す。返り値は次の位置。取れなければ 0 */
static char *nextword(char *s, char *w, int cap) {
  int k;
  s = skipb(s);
  while (*s == '\n') s = skipb(s + 1);
  if (*s == 0) return 0;
  k = 0;
  while (*s && !isblank_(*s) && *s != '\n') {
    if (k + 1 >= cap) die("word too long", w);
    w[k] = *s;
    k = k + 1;
    s = s + 1;
  }
  w[k] = 0;
  return s;
}

/* out へ語を 1 つ足す (すでに何かあれば空白で継ぐ) */
static void addword(char *out, int *n, char *w, int cap) {
  if (*n + (int)strlen(w) + 2 >= cap) die("expansion too long", w);
  if (*n) { out[*n] = ' '; *n = *n + 1; }
  strcpy(out + *n, w);
  *n = *n + (int)strlen(w);
}

/* 型規則で使うものをここでも使う (実体は「作る」の節にある) */
static int patmatch(char *pat, char *t, char *stem, int cap);
static void patsub(char *pat, char *stem, char *out, int cap);

/* 型 p が w に合うか。% が無ければただの一致 */
static int wmatch(char *p, char *w, char *stem, int cap) {
  if (strchr(p, '%') == 0) return strcmp(p, w) == 0;
  return patmatch(p, w, stem, cap);
}

/* 関数を 1 つ呼ぶ。name は展開済み，body は未展開の引数列。
 * 合えば out へ書いて 1 を返す。知らない名前なら 0 */
static int callfn(char *name, char *body, char *out, int cap) {
  char *av[NARG];
  int na;
  char a0[NEXP];
  char a1[NEXP];
  char a2[NEXP];
  char w[512];
  char stem[512];
  char *p;
  int n;
  int i;

  n = 0;
  out[0] = 0;

  /* ---- 引数を先に展開しないもの ---- */

  if (strcmp(name, "if") == 0) {
    na = splitargs(body, av, NARG);
    if (na < 2) die("if needs 2 or 3 arguments", body);
    expand(av[0], a0, (int)sizeof a0);
    rstrip(a0);
    if (skipb(a0)[0] != 0) expand(av[1], out, cap);
    else if (na >= 3) expand(av[2], out, cap);
    return 1;
  }
  if (strcmp(name, "or") == 0) {
    na = splitargs(body, av, NARG);
    for (i = 0; i < na; i = i + 1) {
      expand(av[i], a0, (int)sizeof a0);
      if (a0[0] != 0) {
        if ((int)strlen(a0) >= cap) die("expansion too long", name);
        strcpy(out, a0);
        return 1;
      }
    }
    return 1;
  }
  if (strcmp(name, "foreach") == 0) {
    char save[NEXP];
    int had;
    na = splitargs(body, av, NARG);
    if (na < 3) die("foreach needs 3 arguments", body);
    expand(av[0], a0, (int)sizeof a0);
    rstrip(a0);
    p = skipb(a0);
    expand(av[1], a1, (int)sizeof a1);
    /* 同じ名前の変数があれば退避して戻す */
    i = vfind(p);
    had = (i >= 0);
    if (had) {
      if ((int)strlen(vval[i]) >= (int)sizeof save)
        die("value too long", p);
      strcpy(save, vval[i]);
    }
    { char *q; q = a1;
      for (;;) {
        q = nextword(q, w, (int)sizeof w);
        if (q == 0) break;
        vset(p, w, V_SIMP);
        expand(av[2], a2, (int)sizeof a2);
        if (a2[0]) addword(out, &n, a2, cap);
      }
    }
    if (had) vset(p, save, V_SIMP);
    else { i = vfind(p); if (i >= 0) vset(p, "", V_SIMP); }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "call") == 0) {
    char save[NARG][256];
    char nmb[16];
    na = splitargs(body, av, NARG);
    expand(av[0], a0, (int)sizeof a0);
    rstrip(a0);
    p = skipb(a0);
    /* $(1)..$(9) を立てる。**元の値は戻す** (入れ子の call のため) */
    for (i = 1; i < na && i < 10; i = i + 1) {
      int k;
      sprintf(nmb, "%d", i);
      k = vfind(nmb);
      save[i][0] = 0;
      if (k >= 0) {
        if ((int)strlen(vval[k]) < (int)sizeof save[i])
          strcpy(save[i], vval[k]);
      }
      expand(av[i], a1, (int)sizeof a1);
      vset(nmb, a1, V_SIMP);
    }
    vget(p, out, cap);
    for (i = 1; i < na && i < 10; i = i + 1) {
      sprintf(nmb, "%d", i);
      vset(nmb, save[i], V_SIMP);
    }
    return 1;
  }

  /* ---- ここから先は引数を先に展開する ---- */

  na = splitargs(body, av, NARG);
  for (i = 0; i < na; i = i + 1) {
    /* av[i] の指す先は後で書き換わらないので，順に展開して控える */
    if (i == 0) expand(av[0], a0, (int)sizeof a0);
    else if (i == 1) expand(av[1], a1, (int)sizeof a1);
    else if (i == 2) expand(av[2], a2, (int)sizeof a2);
  }

  if (strcmp(name, "subst") == 0) {
    char *t;
    int fl;
    if (na < 3) die("subst needs 3 arguments", body);
    fl = (int)strlen(a0);
    t = a2;
    if (fl == 0) {
      if ((int)strlen(t) >= cap) die("expansion too long", name);
      strcpy(out, t);
      return 1;
    }
    while (*t) {
      if (strncmp(t, a0, (size_t)fl) == 0) {
        if (n + (int)strlen(a1) + 1 >= cap) die("expansion too long", name);
        strcpy(out + n, a1);
        n = n + (int)strlen(a1);
        t = t + fl;
      } else {
        if (n + 2 >= cap) die("expansion too long", name);
        out[n] = *t;
        n = n + 1;
        t = t + 1;
      }
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "patsubst") == 0) {
    if (na < 3) die("patsubst needs 3 arguments", body);
    p = a2;
    for (;;) {
      p = nextword(p, w, (int)sizeof w);
      if (p == 0) break;
      if (wmatch(a0, w, stem, (int)sizeof stem)) {
        char r[512];
        patsub(a1, stem, r, (int)sizeof r);
        addword(out, &n, r, cap);
      } else {
        addword(out, &n, w, cap);
      }
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "filter") == 0 || strcmp(name, "filter-out") == 0) {
    int want;
    want = (strcmp(name, "filter") == 0);
    if (na < 2) die("filter needs 2 arguments", body);
    p = a1;
    for (;;) {
      char *q;
      int hit;
      p = nextword(p, w, (int)sizeof w);
      if (p == 0) break;
      hit = 0;
      q = a0;
      for (;;) {
        char pat[512];
        q = nextword(q, pat, (int)sizeof pat);
        if (q == 0) break;
        if (wmatch(pat, w, stem, (int)sizeof stem)) { hit = 1; break; }
      }
      if (hit == want) addword(out, &n, w, cap);
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "findstring") == 0) {
    if (na < 2) die("findstring needs 2 arguments", body);
    if (a0[0] && strstr(a1, a0) != 0) {
      if ((int)strlen(a0) >= cap) die("expansion too long", name);
      strcpy(out, a0);
    } else if (a0[0] == 0) {
      out[0] = 0;
    }
    return 1;
  }
  if (strcmp(name, "addprefix") == 0 || strcmp(name, "addsuffix") == 0) {
    int pre;
    pre = (strcmp(name, "addprefix") == 0);
    if (na < 2) die("addprefix / addsuffix needs 2 arguments", body);
    p = a1;
    for (;;) {
      char r[512];
      p = nextword(p, w, (int)sizeof w);
      if (p == 0) break;
      if (pre) { strcpy(r, a0); strcat(r, w); }
      else { strcpy(r, w); strcat(r, a0); }
      addword(out, &n, r, cap);
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "firstword") == 0) {
    if (nextword(a0, w, (int)sizeof w) != 0) {
      if ((int)strlen(w) >= cap) die("expansion too long", name);
      strcpy(out, w);
    }
    return 1;
  }
  if (strcmp(name, "strip") == 0) {
    p = a0;
    for (;;) {
      p = nextword(p, w, (int)sizeof w);
      if (p == 0) break;
      addword(out, &n, w, cap);
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "wildcard") == 0) {
    /* **並べ替えはしない。** 我々の OS には glob が無く，ここでは
     * 「その名前が在るか」だけを見る。型は扱わない —— 扱えないものを
     * 扱えるふりをすると，空に展開されて黙って違う結果になる */
    if (strchr(a0, '*') != 0 || strchr(a0, '?') != 0)
      die("wildcard with * or ? is not implemented", a0);
    p = a0;
    for (;;) {
      p = nextword(p, w, (int)sizeof w);
      if (p == 0) break;
      if (exists(w)) addword(out, &n, w, cap);
    }
    out[n] = 0;
    return 1;
  }
  if (strcmp(name, "shell") == 0) {
    int fd;
    int r;
    char *av2[3];
    fd = open(TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) die("cannot create", TMP);
    write(fd, a0, (int)strlen(a0));
    write(fd, "\n", 1);
    close(fd);
    fflush(stdout);
    av2[0] = "/bin/sh2";
    av2[1] = TMP;
    av2[2] = 0;
    spawn2("/bin/sh2", av2, 0, SHOUT, 0);
    fd = open(SHOUT, O_RDONLY);
    if (fd < 0) return 1;
    n = 0;
    for (;;) {
      r = read(fd, out + n, cap - 1 - n);
      if (r <= 0) break;
      n = n + r;
      if (n >= cap - 1) die("shell output too long", a0);
    }
    close(fd);
    /* 末尾の改行は落とし，中の改行は空白にする (本物と同じ) */
    while (n > 0 && out[n - 1] == '\n') n = n - 1;
    for (i = 0; i < n; i = i + 1) if (out[i] == '\n') out[i] = ' ';
    out[n] = 0;
    return 1;
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
        /* 関数の呼び出し。名前は先頭の語である */
        char fname[64];
        char *body;
        int k;
        k = 0;
        body = nm;
        while (*body && !isblank_(*body)) {
          if (k + 1 >= (int)sizeof fname) break;
          fname[k] = *body;
          k = k + 1;
          body = body + 1;
        }
        fname[k] = 0;
        body = skipb(body);
        if (!callfn(fname, body, val, (int)sizeof val)) {
          /* **黙って空に展開しない。** 変数名として引くと，知らない
           * 関数が静かに空になり，結果だけが違うものになる */
          die("unknown function", fname);
        }
      } else {
        /* **名前を先に展開する。** $($T_FILES) が通るのはここである */
        expand(nm, nm2, (int)sizeof nm2);
        vget(nm2, val, (int)sizeof val);
      }
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
  /* 直前の目標行が立てた規則。**命令行はその全部に付く** ——
   * `m1 m2:` と書いた行の命令は m1 にも m2 にも効く */
  int curr[64];
  int ncurr;
  ncurr = 0;
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
        /* 継続。**前後の空白ごと空白 1 つに畳む** (GNU make と同じ)。
         * 畳まないと `a \` + `   b` が "a  b" になる */
        len = len - 1;
        while (len > 0 && isblank_(line[len - 1])) len = len - 1;
        line[len] = ' ';
        len = len + 1;
        line[len] = 0;
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
      int k;
      if (ncurr == 0) die("recipe line without a rule", line);
      /* 実体は 1 つだけ積み，どの規則からも同じ位置を指す */
      for (k = 0; k < ncurr; k = k + 1) {
        if (rcmdn[curr[k]] == 0) rcmd0[curr[k]] = nlist;
        rcmdn[curr[k]] = rcmdn[curr[k]] + 1;
      }
      addlist(line + 1);        /* 頭の TAB を落とす */
      continue;
    }

    if (skipb(line)[0] == 0) { ncurr = 0; continue; }

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
        ncurr = 0;
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
        ncurr = 0;
        continue;
      }

      if (colon) {
        char tg[NEXP];
        char dp[NEXP];
        char *q2;
        *colon = 0;
        ncurr = 0;
        expand(skipb(line), tg, (int)sizeof tg);
        expand(skipb(colon + 1), dp, (int)sizeof dp);
        rstrip(tg);
        rstrip(dp);
        /* 目標が複数並ぶことがある。それぞれに同じ規則を立てる */
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
            ncurr = 0;
            break;
          }
          ri = addrule(one);
          if (ncurr < 64) { curr[ncurr] = ri; ncurr = ncurr + 1; }
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
        continue;
      }
    }
    die("cannot parse line", line);
  }
}

/* 1 つの記述を読んで表に足す。required が 0 なら無くてもよい。
 *
 * **器は呼ぶたびに取る。** 静的な 1 つを使い回すと，include が
 * 外側の parselines が読んでいる最中の器を上書きしてしまう
 * (cc18 の adddir と同じ型の誤り。docs/stage017-cc.md 8.1) */
static void parse(char *path, int required) {
  char *buf;
  buf = malloc(NTXT);
  if (buf == 0) die("out of memory reading", path);
  if (slurp(path, buf, NTXT) < 0) {
    free(buf);
    if (required) die("cannot open", path);
    return;
  }
  parselines(buf, path);
  free(buf);
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

/* 目標 t を作り直す要りがあるか (第 4 部の 2)。
 *
 * 依存はすべて作り終えた後で呼ぶこと。**先に作らないと，これから
 * 作られる依存を「無い」と見て毎回作り直す。** */
static int needs(int ri, char *t, char *stem) {
  struct stat ts;
  struct stat ds;
  char dep[512];
  int i;

  /* .PHONY は中身を持たない。同名のファイルがあっても必ず作る */
  if (always) return 1;
  if (isphony(t)) return 1;
  if (stat(t, &ts) < 0) return 1;               /* まだ無い */
  for (i = 0; i < rdepn[ri]; i = i + 1) {
    patsub(list[rdep0[ri] + i], stem, dep, (int)sizeof dep);
    /* **依存が見当たらないなら作り直す。** 作れなかったのか消えたのか
     * ここでは判らないが，「古いまま使う」より安全である */
    if (stat(dep, &ds) < 0) return 1;
    if (newer(&ds, &ts)) return 1;
  }
  return 0;
}

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
  built = 1;
  /* -n では @ の行も出す。**出さないと「何を走らせる気なのか」を
   * 見るための -n が用を成さない** (本物の make も出す) */
  if ((!quiet || dryrun) && !silent) {
    fputs(c, stdout);
    fputs("\n", stdout);
  }
  if (dryrun) return;
  /* 我々の stdio は緩衝しないので何もしないが，ホストで一巡させる
   * ときは順序が狂う。**同じ道を通す**ために置く */
  fflush(stdout);

  fd = open(TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) die("cannot create", TMP);
  write(fd, c, (int)strlen(c));
  write(fd, "\n", 1);
  close(fd);

  av[0] = "/bin/sh2";
  av[1] = TMP;
  av[2] = 0;
  st = spawn2("/bin/sh2", av, 0, 0, 0);
  if (st == 0) return;
  {
    char n[32];
    sprintf(n, "%d", st);
    /* **無視するときも黙らない。** 「失敗したが続けた」を黙って
     * 呑むのは，我々が台帳で bad と呼んでいるものそのものである */
    fputs(ignore ? "mk: [" : "mk: *** [", stderr);
    fputs(a_tgt ? a_tgt : "?", stderr);
    fputs("] error ", stderr);
    fputs(n, stderr);
    fputs(ignore ? " (ignored)\n" : "\n", stderr);
  }
  if (!ignore) exit(2);
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

  /* 依存が揃ったところで古さを見る (第 4 部の 2) */
  if (!needs(ri, t, stem)) return;

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
  fputs("usage: mk [-f makefile] [-n] [-s] [-B] [target...]\n", stderr);
  fputs("  -B  古さを見ずに必ず作る\n", stderr);
  fputs("  依存より古い目標だけを作り直す (sfs3 の時刻を見る。"
        "docs/stage017-cc.md 11 章)\n", stderr);
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
    if (strcmp(a, "-B") == 0) { always = 1; continue; }
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

  built = 0;
  for (i = 0; i < ngoal; i = i + 1) make(goals[i]);
  /* **黙って終わらない。** 作らなかったのか作れなかったのかが
   * 呼び手に判らなくなる (本物の make と同じ形にしてある) */
  if (!built && !silent) {
    fputs("mk: nothing to be done for '", stdout);
    fputs(goals[0], stdout);
    fputs("'.\n", stdout);
  }
  return 0;
}
