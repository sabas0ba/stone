/* awk1.c --- awk の第 1 世代 (docs/stage017-gcc.md 5.8)
 *
 * `configure` が使う道具のうち，最後に残っていたものである。autoconf の
 * `config.status` は awk 無しでは 1 行も書き出せない。
 *
 * ## 何を作ったか
 *
 * POSIX awk の部分集合。**言語ひとつぶん**なので，構造は
 *
 *   字句 (lex) → 構文木 (parse) → 木を歩く (exec)
 *
 * の 3 段で，これは `sh2` や `cc15` と同じ形である。正規表現は自分で
 * 持たず **`re2` を使う** —— awk が要るのは ERE で，`re2` に ERE を
 * 入れたのはそもそもこのためである (5.7)。
 *
 * ## 値は 2 つの顔を持つ
 *
 * awk の値は文字列でも数でもある。入力から来た値は「数に見えるなら数
 * として比べる」(strnum)。この規則を外すと `$1 == 0` の類が黙って違う
 * 答になるので，型を 4 つ (未設定・数・文字列・数に見える文字列) 持って
 * 区別する。
 *
 * ## 数を文字列にする形は自前で持つ
 *
 * `%.6g` (CONVFMT / OFMT の既定) は libc22 の printf が持っていない ——
 * `%g` を `%f` と同じに扱っている。awk の出力はほとんどがこの変換を
 * 通るので，**ここだけは自前で書く**。libc の穴は libc の世代で直す
 * べきもので，awk の側で測るものではない (5.8 の註)。
 *
 * ## 持たないもの (要らないと確かめた)
 *
 *   パイプ (`cmd | getline` / `print | cmd`)  カーネルがパイプを持たない
 *   system()                                  同上
 *   RS が 1 文字でない形 (段落単位)           configure に現れない
 *   数学関数 (sin/cos/log/exp/sqrt/atan2)     libm が無い。int() は持つ
 *   ENVIRON                                   環境変数の概念が無い
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "re2.h"
#include "awkfmt1.h"

#define MAXSRC 65536
#define MAXSTR 8192
#define NNODE 8192
#define NVAR 512
#define NELEM 16384
#define NHASH 1024
#define NFIELD 512
#define ARENA 1048576
#define VSTK 512
#define NFUN 64
#define NPARAM 24
#define NRE 128
#define NFRAME 64
#define NOUTF 16
#define NINF 16
#define LINEMAX 32768

/* 行ほどの大きさの入れ物は**関数の枠に積まない**。我々の cc は 1 つの
 * 関数の枠を 65528 バイトまでしか作れず (cc15aa の上限)，32 KiB の
 * 配列を 2 つ積むだけで越える。入れ子にならない使い方だけなので，
 * 場所を分けて静的に持つ */
static char bufsplit[LINEMAX];
static char bufsplitin[LINEMAX];
static char bufrec[LINEMAX];
static char bufbuild[LINEMAX];
static char bufsub[LINEMAX];
static char bufout[LINEMAX];
static char bufgetline[LINEMAX];

static char *progname;

static int die(char *msg, char *arg) {
  fflush(stdout);
  fprintf(stderr, "awk: %s", msg);
  if (arg) fprintf(stderr, ": %s", arg);
  fprintf(stderr, "\n");
  exit(2);
  return 0;
}

/* ================= 作業用の置き場 =================
 *
 * 式の途中で作る文字列は**式が終われば要らない**。1 文ごとに印を戻す
 * 置き場に取る (malloc し放しにすると gsub の繰返しで食い潰す)。
 * 変数や配列へ入れる値だけは malloc して控える */
static char arena[ARENA];
static int ap;

static char *aalloc(int n) {
  char *p;
  if (ap + n + 1 > ARENA) die("out of string space", 0);
  p = arena + ap;
  ap = ap + n + 1;
  p[n] = 0;
  return p;
}

static char *acopy(char *s) {
  int n;
  char *p;
  n = (int)strlen(s);
  p = aalloc(n);
  memcpy(p, s, n);
  return p;
}

/* 数を字にする小さな道具。配列の鍵を作るのに要る (書式まわりは
 * awkfmt1 にあるが，ここで要るのはこれだけである) */
static int itostr(long long v, char *out) {
  char tmp[24];
  int n;
  int i;
  int neg;
  neg = v < 0;
  if (neg) v = -v;
  n = 0;
  if (v == 0) { tmp[n] = '0'; n = 1; }
  while (v > 0) { tmp[n] = (char)('0' + (int)(v % 10)); v = v / 10; n = n + 1; }
  i = 0;
  if (neg) { out[0] = '-'; i = 1; }
  while (n > 0) { n = n - 1; out[i] = tmp[n]; i = i + 1; }
  out[i] = 0;
  return i;
}

static char *dupstr(char *s) {
  char *p;
  p = (char *)malloc(strlen(s) + 1);
  if (p == 0) die("out of memory", 0);
  strcpy(p, s);
  return p;
}

/* ================= 値 =================
 *
 * 型は 4 つ。**入力から来た値 (T_SN) は数として比べる**のが awk の
 * 規則で，これを落とすと `$1 == 0` の類が黙って違う答になる */
#define V_UNINIT 0
#define V_NUM 1
#define V_STR 2
#define V_SN 3                  /* 入力由来で数に見えるもの */

static int vtp[VSTK];
static double vnm[VSTK];
static char *vst[VSTK];
static int sp;

/* 数に見えるか (前後の空白は許す) */
static int looksnum(char *s) {
  char *e;
  if (s == 0) return 0;
  while (*s == ' ' || *s == '\t' || *s == '\n') s = s + 1;
  if (*s == 0) return 0;
  strtod(s, &e);
  if (e == s) return 0;
  while (*e == ' ' || *e == '\t' || *e == '\n') e = e + 1;
  return *e == 0;
}

static int pushnum(double d) {
  if (sp >= VSTK) die("expression too deep", 0);
  vtp[sp] = V_NUM;
  vnm[sp] = d;
  vst[sp] = 0;
  sp = sp + 1;
  return 0;
}

static int pushstr(char *s) {
  if (sp >= VSTK) die("expression too deep", 0);
  vtp[sp] = V_STR;
  vnm[sp] = 0;
  vst[sp] = s;
  sp = sp + 1;
  return 0;
}

/* 入力由来の値。数に見えるなら数としても比べられる形で積む */
static int pushin(char *s) {
  pushstr(s);
  if (looksnum(s)) {
    vtp[sp - 1] = V_SN;
    vnm[sp - 1] = strtod(s, (char **)0);
  }
  return 0;
}

static int pushuninit(void) {
  if (sp >= VSTK) die("expression too deep", 0);
  vtp[sp] = V_UNINIT;
  vnm[sp] = 0;
  vst[sp] = "";
  sp = sp + 1;
  return 0;
}

static char *convfmt(void);
static char *ofmt(void);

/* 数を字にする。中身は awkfmt1 にある */
static char *numstr(double d, char *fmt) {
  char buf[512];
  awk_numstr(d, fmt, buf);
  return acopy(buf);
}

static char *tostr(int i) {
  if (vtp[i] == V_NUM) return numstr(vnm[i], convfmt());
  if (vtp[i] == V_UNINIT) return "";
  return vst[i];
}

/* 出力の形。print は OFMT を通す (CONVFMT ではない) */
static char *tostro(int i) {
  if (vtp[i] == V_NUM) return numstr(vnm[i], ofmt());
  if (vtp[i] == V_UNINIT) return "";
  return vst[i];
}

static double tonum(int i) {
  if (vtp[i] == V_NUM || vtp[i] == V_SN) return vnm[i];
  if (vtp[i] == V_UNINIT) return 0.0;
  return strtod(vst[i], (char **)0);
}

static int tobool(int i) {
  if (vtp[i] == V_NUM || vtp[i] == V_SN) return vnm[i] != 0.0;
  if (vtp[i] == V_UNINIT) return 0;
  return vst[i][0] != 0;
}

/* ================= 変数と配列 ================= */

static char *vname[NVAR];
static int vkind[NVAR];         /* 0 未定 / 1 スカラ / 2 配列 */
static int vtyp[NVAR];
static double vnum[NVAR];
static char *vstr[NVAR];        /* malloc して持つ */
static int varrid[NVAR];
static int nvar;

#define SV_NR 0
#define SV_NF 1
#define SV_FS 2
#define SV_OFS 3
#define SV_ORS 4
#define SV_RS 5
#define SV_FILENAME 6
#define SV_SUBSEP 7
#define SV_RSTART 8
#define SV_RLENGTH 9
#define SV_CONVFMT 10
#define SV_OFMT 11
#define SV_FNR 12
#define NSPECIAL 13

static int narr;

static int lookvar(char *nm) {
  int i;
  for (i = 0; i < nvar; i = i + 1) {
    if (strcmp(vname[i], nm) == 0) return i;
  }
  if (nvar >= NVAR) die("too many variables", 0);
  i = nvar;
  nvar = nvar + 1;
  vname[i] = dupstr(nm);
  vkind[i] = 0;
  vtyp[i] = V_UNINIT;
  vnum[i] = 0;
  vstr[i] = 0;
  varrid[i] = -1;
  return i;
}

static int setvnum(int v, double d) {
  if (vstr[v]) { free(vstr[v]); vstr[v] = 0; }
  vkind[v] = 1;
  vtyp[v] = V_NUM;
  vnum[v] = d;
  return 0;
}

static int setvstr(int v, char *s, int isin) {
  char *d;
  d = dupstr(s);
  if (vstr[v]) free(vstr[v]);
  vstr[v] = d;
  vkind[v] = 1;
  vtyp[v] = V_STR;
  vnum[v] = 0;
  if (isin && looksnum(s)) {
    vtyp[v] = V_SN;
    vnum[v] = strtod(s, (char **)0);
  }
  return 0;
}

static char *convfmt(void) {
  if (vstr[SV_CONVFMT]) return vstr[SV_CONVFMT];
  return "%.6g";
}
static char *ofmt(void) {
  if (vstr[SV_OFMT]) return vstr[SV_OFMT];
  return "%.6g";
}
static char *svstr(int v) {
  if (vtyp[v] == V_NUM) return numstr(vnum[v], convfmt());
  if (vtyp[v] == V_UNINIT || vstr[v] == 0) return "";
  return vstr[v];
}
static double svnum(int v) {
  if (vtyp[v] == V_NUM || vtyp[v] == V_SN) return vnum[v];
  if (vtyp[v] == V_UNINIT || vstr[v] == 0) return 0.0;
  return strtod(vstr[v], (char **)0);
}

/* ---- 配列 ---- */
static int hbkt[NHASH];
static int enxt[NELEM];
static int earr[NELEM];
static char *ekey[NELEM];
static int etyp[NELEM];
static double enm[NELEM];
static char *estr[NELEM];
static int edel[NELEM];
static int ecnt;

static int hashof(int arr, char *k) {
  unsigned int h;
  h = (unsigned int)arr * 31u;
  while (*k) { h = h * 131u + (unsigned int)(unsigned char)*k; k = k + 1; }
  return (int)(h % NHASH);
}

static int findel(int arr, char *k, int create) {
  int b;
  int i;
  b = hashof(arr, k);
  for (i = hbkt[b]; i >= 0; i = enxt[i]) {
    if (!edel[i] && earr[i] == arr && strcmp(ekey[i], k) == 0) return i;
  }
  if (!create) return -1;
  if (ecnt >= NELEM) die("array too large", 0);
  i = ecnt;
  ecnt = ecnt + 1;
  earr[i] = arr;
  ekey[i] = dupstr(k);
  etyp[i] = V_UNINIT;
  enm[i] = 0;
  estr[i] = 0;
  edel[i] = 0;
  enxt[i] = hbkt[b];
  hbkt[b] = i;
  return i;
}

static int delel(int arr, char *k) {
  int i;
  i = findel(arr, k, 0);
  if (i < 0) return 0;
  edel[i] = 1;
  if (estr[i]) { free(estr[i]); estr[i] = 0; }
  return 0;
}

static int delarr(int arr) {
  int i;
  for (i = 0; i < ecnt; i = i + 1) {
    if (earr[i] == arr && !edel[i]) {
      edel[i] = 1;
      if (estr[i]) { free(estr[i]); estr[i] = 0; }
    }
  }
  return 0;
}

/* ================= 関数の枠 ================= */
static int fnpar[NFRAME][NPARAM];       /* 枠のスカラ値 */
static double fnpnum[NFRAME][NPARAM];
static char *fnpstr[NFRAME][NPARAM];
static int fnparr[NFRAME][NPARAM];      /* 配列として渡されていれば番号 */
static int fdepth;

/* ================= 欄 ($0 と $1..$NF) ================= */
static char rec[LINEMAX];
static char *fld[NFIELD];               /* malloc して持つ */
static int nfld;

static int setnf(int n) {
  vtyp[SV_NF] = V_NUM;
  vnum[SV_NF] = (double)n;
  if (vstr[SV_NF]) { free(vstr[SV_NF]); vstr[SV_NF] = 0; }
  return 0;
}

static int clearflds(void) {
  int i;
  for (i = 1; i <= nfld; i = i + 1) {
    if (fld[i]) { free(fld[i]); fld[i] = 0; }
  }
  nfld = 0;
  return 0;
}

static int recache;                     /* 動的な正規表現の控え */
static char *recpat[NRE];
static int recere[NRE];
static int rechead[NRE];

static int getre(char *pat, int isere) {
  int i;
  int h;
  for (i = 0; i < recache; i = i + 1) {
    if (recere[i] == isere && strcmp(recpat[i], pat) == 0) return rechead[i];
  }
  if (isere) h = re_compile_ere(pat);
  else h = re_compile(pat);
  if (h < 0) die(re_errmsg(), pat);
  if (recache < NRE) {
    recpat[recache] = dupstr(pat);
    recere[recache] = isere;
    rechead[recache] = h;
    recache = recache + 1;
  }
  return h;
}

/* 1 本の文字列を FS で切り分ける。切り先を配列 (arr >= 0) か欄へ入れる */
static int splitrec(char *s, char *fs, int arr) {
  char *p;
  char *q;
  char *buf;
  int n;
  int h;
  char *mb;
  char *me;
  int len;
  buf = bufsplit;
  n = 0;
  p = s;
  if (fs[0] == ' ' && fs[1] == 0) {
    /* 既定。前後の空白を落とし，空白の並びで切る */
    while (*p == ' ' || *p == '\t' || *p == '\n') p = p + 1;
    while (*p) {
      q = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n') p = p + 1;
      len = (int)(p - q);
      if (len >= LINEMAX) die("field too long", 0);
      memcpy(buf, q, (size_t)len);
      buf[len] = 0;
      n = n + 1;
      if (arr >= 0) {
        char kb[32];
        int e;
        itostr((long long)n, kb);
        e = findel(arr, kb, 1);
        if (estr[e]) free(estr[e]);
        estr[e] = dupstr(buf);
        etyp[e] = V_STR;
        enm[e] = 0;
        if (looksnum(buf)) { etyp[e] = V_SN; enm[e] = strtod(buf, (char **)0); }
      } else {
        if (n >= NFIELD) die("too many fields", 0);
        if (fld[n]) free(fld[n]);
        fld[n] = dupstr(buf);
      }
      while (*p == ' ' || *p == '\t' || *p == '\n') p = p + 1;
    }
    if (arr < 0) nfld = n;
    return n;
  }
  h = -1;
  if (fs[1] != 0) h = getre(fs, 1);
  while (1) {
    if (h < 0) {
      q = strchr(p, fs[0]);
      if (q) { mb = q; me = q + 1; } else { mb = 0; me = 0; }
    } else {
      if (!re_search(h, p, &mb, &me)) { mb = 0; me = 0; }
      if (re_overrun()) die("regexp too complex", 0);
      if (mb && me == mb) {                 /* 空に合う形は 1 字進める */
        if (*p == 0) { mb = 0; me = 0; }
        else { mb = p + 1; me = p + 1; }
      }
    }
    if (mb) len = (int)(mb - p);
    else len = (int)strlen(p);
    if (len >= LINEMAX) die("field too long", 0);
    memcpy(buf, p, (size_t)len);
    buf[len] = 0;
    n = n + 1;
    if (arr >= 0) {
      char kb[32];
      int e;
      itostr((long long)n, kb);
      e = findel(arr, kb, 1);
      if (estr[e]) free(estr[e]);
      estr[e] = dupstr(buf);
      etyp[e] = V_STR;
      enm[e] = 0;
      if (looksnum(buf)) { etyp[e] = V_SN; enm[e] = strtod(buf, (char **)0); }
    } else {
      if (n >= NFIELD) die("too many fields", 0);
      if (fld[n]) free(fld[n]);
      fld[n] = dupstr(buf);
    }
    if (!mb) break;
    p = me;
  }
  if (arr < 0) nfld = n;
  return n;
}

static char *fsval(void) {
  if (vstr[SV_FS]) return vstr[SV_FS];
  return " ";
}
static char *ofsval(void) {
  if (vstr[SV_OFS]) return vstr[SV_OFS];
  return " ";
}
static char *orsval(void) {
  if (vstr[SV_ORS]) return vstr[SV_ORS];
  return "\n";
}
static char *subsepval(void) {
  if (vstr[SV_SUBSEP]) return vstr[SV_SUBSEP];
  return "\034";
}

static int setrec(char *s) {
  int n;
  char *tmp;
  tmp = bufrec;
  n = (int)strlen(s);
  if (n >= LINEMAX) die("record too long", 0);
  /* **s が rec や欄の中を指していることがある** ($0 = $0 / $1 = $1)。
   * 先に写してから解放しないと，解放済みの領域を読む */
  memcpy(tmp, s, (size_t)n);
  tmp[n] = 0;
  clearflds();
  memcpy(rec, tmp, (size_t)n);
  rec[n] = 0;
  splitrec(rec, fsval(), -1);
  setnf(nfld);
  return 0;
}

/* 欄から $0 を組み直す */
static int rebuild(void) {
  char *buf;
  int i;
  int n;
  char *o;
  buf = bufbuild;
  n = 0;
  o = ofsval();
  for (i = 1; i <= nfld; i = i + 1) {
    int l;
    if (i > 1) {
      l = (int)strlen(o);
      if (n + l >= LINEMAX) die("record too long", 0);
      memcpy(buf + n, o, (size_t)l);
      n = n + l;
    }
    if (fld[i]) {
      l = (int)strlen(fld[i]);
      if (n + l >= LINEMAX) die("record too long", 0);
      memcpy(buf + n, fld[i], (size_t)l);
      n = n + l;
    }
  }
  buf[n] = 0;
  memcpy(rec, buf, (size_t)(n + 1));
  return 0;
}

static char *getfield(int i) {
  if (i == 0) return rec;
  if (i < 0) die("negative field", 0);
  if (i > nfld) return "";
  if (fld[i] == 0) return "";
  return fld[i];
}

static int setfield(int i, char *s) {
  int k;
  char *d;
  if (i == 0) { setrec(s); return 0; }
  if (i < 0) die("negative field", 0);
  if (i >= NFIELD) die("too many fields", 0);
  /* **先に複製する** —— s が書き換える欄そのものを指していることがある
   * ($1 = $1 は autoconf の生成する awk に実際に現れる) */
  d = dupstr(s);
  for (k = nfld + 1; k <= i; k = k + 1) {
    if (fld[k]) free(fld[k]);
    fld[k] = dupstr("");
  }
  if (i > nfld) { nfld = i; setnf(nfld); }
  if (fld[i]) free(fld[i]);
  fld[i] = d;
  rebuild();
  return 0;
}

/* ================= 字句 ================= */

#define T_EOF 256
#define T_NL 257
#define T_NUM 258
#define T_STR 259
#define T_ERE 260
#define T_NAME 261
#define T_FUNCNAME 262
#define T_BUILTIN 263
#define T_GETLINE 264
#define T_BEGIN 265
#define T_END 266
#define T_FUNCTION 267
#define T_IF 268
#define T_ELSE 269
#define T_WHILE 270
#define T_FOR 271
#define T_DO 272
#define T_BREAK 273
#define T_CONTINUE 274
#define T_NEXT 275
#define T_EXIT 276
#define T_RETURN 277
#define T_DELETE 278
#define T_IN 279
#define T_PRINT 280
#define T_PRINTF 281
#define T_OROR 282
#define T_ANDAND 283
#define T_NOMATCH 284
#define T_EQ 285
#define T_NE 286
#define T_LE 287
#define T_GE 288
#define T_INCR 289
#define T_DECR 290
#define T_APPEND 291
#define T_ADDA 292
#define T_SUBA 293
#define T_MULA 294
#define T_DIVA 295
#define T_MODA 296
#define T_POWA 297

#define B_LENGTH 1
#define B_SUBSTR 2
#define B_INDEX 3
#define B_SPLIT 4
#define B_SUB 5
#define B_GSUB 6
#define B_MATCH 7
#define B_SPRINTF 8
#define B_TOUPPER 9
#define B_TOLOWER 10
#define B_INT 11
#define B_CLOSE 12
#define B_SRAND 13
#define B_RAND 14

static char src[MAXSRC];
static int spos;
static int tok;
static double tnum;
static char tstr[MAXSTR];
static int tval;                /* NAME の変数番号 / BUILTIN の番号 */
static int prevoperand;
static int lasttok;

static int iddigit(int c) { return c >= '0' && c <= '9'; }
static int idalpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int kwof(char *s) {
  if (strcmp(s, "BEGIN") == 0) return T_BEGIN;
  if (strcmp(s, "END") == 0) return T_END;
  if (strcmp(s, "function") == 0) return T_FUNCTION;
  if (strcmp(s, "func") == 0) return T_FUNCTION;
  if (strcmp(s, "if") == 0) return T_IF;
  if (strcmp(s, "else") == 0) return T_ELSE;
  if (strcmp(s, "while") == 0) return T_WHILE;
  if (strcmp(s, "for") == 0) return T_FOR;
  if (strcmp(s, "do") == 0) return T_DO;
  if (strcmp(s, "break") == 0) return T_BREAK;
  if (strcmp(s, "continue") == 0) return T_CONTINUE;
  if (strcmp(s, "next") == 0) return T_NEXT;
  if (strcmp(s, "exit") == 0) return T_EXIT;
  if (strcmp(s, "return") == 0) return T_RETURN;
  if (strcmp(s, "delete") == 0) return T_DELETE;
  if (strcmp(s, "in") == 0) return T_IN;
  if (strcmp(s, "print") == 0) return T_PRINT;
  if (strcmp(s, "printf") == 0) return T_PRINTF;
  if (strcmp(s, "getline") == 0) return T_GETLINE;
  return 0;
}

static int biof(char *s) {
  if (strcmp(s, "length") == 0) return B_LENGTH;
  if (strcmp(s, "substr") == 0) return B_SUBSTR;
  if (strcmp(s, "index") == 0) return B_INDEX;
  if (strcmp(s, "split") == 0) return B_SPLIT;
  if (strcmp(s, "sub") == 0) return B_SUB;
  if (strcmp(s, "gsub") == 0) return B_GSUB;
  if (strcmp(s, "match") == 0) return B_MATCH;
  if (strcmp(s, "sprintf") == 0) return B_SPRINTF;
  if (strcmp(s, "toupper") == 0) return B_TOUPPER;
  if (strcmp(s, "tolower") == 0) return B_TOLOWER;
  if (strcmp(s, "int") == 0) return B_INT;
  if (strcmp(s, "close") == 0) return B_CLOSE;
  if (strcmp(s, "srand") == 0) return B_SRAND;
  if (strcmp(s, "rand") == 0) return B_RAND;
  return 0;
}

/* 逆斜線の後ろ 1 字 */
static int escof(int c) {
  if (c == 'n') return '\n';
  if (c == 't') return '\t';
  if (c == 'r') return '\r';
  if (c == 'b') return '\b';
  if (c == 'f') return '\f';
  if (c == 'v') return '\v';
  if (c == 'a') return 7;
  return c;
}

static int gettok(void) {
  int c;
  int n;
  int skipnl;
  skipnl = (lasttok == '{' || lasttok == T_ANDAND || lasttok == T_OROR
            || lasttok == ',' || lasttok == ';' || lasttok == T_DO
            || lasttok == T_ELSE || lasttok == T_NL || lasttok == 0
            || lasttok == '}');
  while (1) {
    c = (unsigned char)src[spos];
    if (c == ' ' || c == '\t' || c == '\r') { spos = spos + 1; continue; }
    if (c == '\\' && src[spos + 1] == '\n') { spos = spos + 2; continue; }
    if (c == '#') {
      while (src[spos] && src[spos] != '\n') spos = spos + 1;
      continue;
    }
    if (c == '\n' && skipnl) { spos = spos + 1; continue; }
    break;
  }
  c = (unsigned char)src[spos];
  if (c == 0) { lasttok = T_EOF; prevoperand = 0; return T_EOF; }
  if (c == '\n') { spos = spos + 1; lasttok = T_NL; prevoperand = 0; return T_NL; }
  if (iddigit(c) || (c == '.' && iddigit((unsigned char)src[spos + 1]))) {
    char *e;
    tnum = strtod(src + spos, &e);
    spos = (int)(e - src);
    lasttok = T_NUM;
    prevoperand = 1;
    return T_NUM;
  }
  if (idalpha(c)) {
    int k;
    n = 0;
    while (idalpha((unsigned char)src[spos]) || iddigit((unsigned char)src[spos])) {
      if (n < MAXSTR - 1) { tstr[n] = src[spos]; n = n + 1; }
      spos = spos + 1;
    }
    tstr[n] = 0;
    k = kwof(tstr);
    if (k) {
      lasttok = k;
      prevoperand = 0;
      return k;
    }
    k = biof(tstr);
    if (k) {
      tval = k;
      lasttok = T_BUILTIN;
      prevoperand = 0;
      return T_BUILTIN;
    }
    prevoperand = 1;
    if (src[spos] == '(') { lasttok = T_FUNCNAME; return T_FUNCNAME; }
    lasttok = T_NAME;
    return T_NAME;
  }
  if (c == '"') {
    spos = spos + 1;
    n = 0;
    while (src[spos] && src[spos] != '"') {
      if (src[spos] == '\\' && src[spos + 1]) {
        spos = spos + 1;
        if (n < MAXSTR - 1) { tstr[n] = (char)escof((unsigned char)src[spos]); n = n + 1; }
        spos = spos + 1;
        continue;
      }
      if (n < MAXSTR - 1) { tstr[n] = src[spos]; n = n + 1; }
      spos = spos + 1;
    }
    if (src[spos] != '"') die("unterminated string", 0);
    spos = spos + 1;
    tstr[n] = 0;
    lasttok = T_STR;
    prevoperand = 1;
    return T_STR;
  }
  if (c == '/' && !prevoperand) {
    /* 正規表現。**割り算と読み分けるのは直前の字句で決める** */
    spos = spos + 1;
    n = 0;
    while (src[spos] && src[spos] != '/') {
      if (src[spos] == '\\' && src[spos + 1] == '/') {
        if (n < MAXSTR - 1) { tstr[n] = '/'; n = n + 1; }
        spos = spos + 2;
        continue;
      }
      if (src[spos] == '\\' && src[spos + 1]) {
        if (n < MAXSTR - 2) { tstr[n] = '\\'; tstr[n + 1] = src[spos + 1]; n = n + 2; }
        spos = spos + 2;
        continue;
      }
      if (src[spos] == '\n') die("newline in regexp", 0);
      if (n < MAXSTR - 1) { tstr[n] = src[spos]; n = n + 1; }
      spos = spos + 1;
    }
    if (src[spos] != '/') die("unterminated regexp", 0);
    spos = spos + 1;
    tstr[n] = 0;
    lasttok = T_ERE;
    prevoperand = 1;
    return T_ERE;
  }
  spos = spos + 1;
  prevoperand = 0;
  if (c == '|' && src[spos] == '|') { spos = spos + 1; lasttok = T_OROR; return T_OROR; }
  if (c == '&' && src[spos] == '&') { spos = spos + 1; lasttok = T_ANDAND; return T_ANDAND; }
  if (c == '!' && src[spos] == '~') { spos = spos + 1; lasttok = T_NOMATCH; return T_NOMATCH; }
  if (c == '!' && src[spos] == '=') { spos = spos + 1; lasttok = T_NE; return T_NE; }
  if (c == '=' && src[spos] == '=') { spos = spos + 1; lasttok = T_EQ; return T_EQ; }
  if (c == '<' && src[spos] == '=') { spos = spos + 1; lasttok = T_LE; return T_LE; }
  if (c == '>' && src[spos] == '=') { spos = spos + 1; lasttok = T_GE; return T_GE; }
  if (c == '>' && src[spos] == '>') { spos = spos + 1; lasttok = T_APPEND; return T_APPEND; }
  if (c == '+' && src[spos] == '+') { spos = spos + 1; lasttok = T_INCR; return T_INCR; }
  if (c == '-' && src[spos] == '-') { spos = spos + 1; lasttok = T_DECR; return T_DECR; }
  if (c == '+' && src[spos] == '=') { spos = spos + 1; lasttok = T_ADDA; return T_ADDA; }
  if (c == '-' && src[spos] == '=') { spos = spos + 1; lasttok = T_SUBA; return T_SUBA; }
  if (c == '*' && src[spos] == '=') { spos = spos + 1; lasttok = T_MULA; return T_MULA; }
  if (c == '/' && src[spos] == '=') { spos = spos + 1; lasttok = T_DIVA; return T_DIVA; }
  if (c == '%' && src[spos] == '=') { spos = spos + 1; lasttok = T_MODA; return T_MODA; }
  if (c == '^' && src[spos] == '=') { spos = spos + 1; lasttok = T_POWA; return T_POWA; }
  if (c == ')' || c == ']') prevoperand = 1;
  lasttok = c;
  return c;
}

static int nexttok(void) { tok = gettok(); return tok; }

/* ================= 構文木 ================= */

#define N_NUM 1
#define N_STR 2
#define N_RE 3
#define N_VAR 4
#define N_LOCAL 5
#define N_FIELD 6
#define N_INDEX 7
#define N_ASSIGN 8
#define N_COND 9
#define N_OR 10
#define N_AND 11
#define N_NOT 12
#define N_MATCH 13
#define N_CMP 14
#define N_CAT 15
#define N_ARITH 16
#define N_NEG 17
#define N_INCDEC 18
#define N_CALL 19
#define N_BUILTIN 20
#define N_GETLINE 21
#define N_IN 22
#define N_ARGS 23
#define N_GROUP 24

#define S_LIST 40
#define S_PRINT 41
#define S_PRINTF 42
#define S_IF 43
#define S_WHILE 44
#define S_DO 45
#define S_FOR 46
#define S_FORIN 47
#define S_EXPR 48
#define S_NEXT 49
#define S_EXIT 50
#define S_BREAK 51
#define S_CONT 52
#define S_DELETE 53
#define S_RETURN 54
#define S_NOP 55

static int ntyp[NNODE];
static int nl[NNODE];
static int nr[NNODE];
static int nx[NNODE];
static int ny[NNODE];
static int nop[NNODE];
static double nnum[NNODE];
static char *nstr[NNODE];
static int ncnt;

static int mknode(int t, int a, int b) {
  int i;
  if (ncnt >= NNODE) die("program too large", 0);
  i = ncnt;
  ncnt = ncnt + 1;
  ntyp[i] = t;
  nl[i] = a;
  nr[i] = b;
  nx[i] = -1;
  ny[i] = -1;
  nop[i] = 0;
  nnum[i] = 0;
  nstr[i] = 0;
  return i;
}

/* ---- 規則 (pattern { action }) ---- */
#define NRULE 128
#define R_BEGIN 1
#define R_END 2
#define R_PAT 3
#define R_RANGE 4
static int rkind[NRULE];
static int rpat[NRULE];
static int rpat2[NRULE];
static int ract[NRULE];
static int ractive[NRULE];
static int nrule;

/* ---- 関数 ---- */
static char *fname[NFUN];
static int fnpn[NFUN];
static int fbody[NFUN];
static char *fpname[NFUN][NPARAM];
static int nfun;

static int curfun;              /* 解析中の関数 (-1 = 無し) */

static int findfun(char *nm) {
  int i;
  for (i = 0; i < nfun; i = i + 1) {
    if (strcmp(fname[i], nm) == 0) return i;
  }
  return -1;
}

static int addfun(char *nm) {
  int i;
  i = findfun(nm);
  if (i >= 0) return i;
  if (nfun >= NFUN) die("too many functions", 0);
  i = nfun;
  nfun = nfun + 1;
  fname[i] = dupstr(nm);
  fnpn[i] = 0;
  fbody[i] = -1;
  return i;
}

/* 解析中の関数の仮引数なら局所の番号を返す */
static int localof(char *nm) {
  int i;
  if (curfun < 0) return -1;
  for (i = 0; i < fnpn[curfun]; i = i + 1) {
    if (strcmp(fpname[curfun][i], nm) == 0) return i;
  }
  return -1;
}

static int expr(int nogt);
static int stmt(void);
static int nargsof(int args);

static int mkvarnode(char *nm) {
  int k;
  int n;
  k = localof(nm);
  if (k >= 0) {
    n = mknode(N_LOCAL, -1, -1);
    nop[n] = k;
    return n;
  }
  n = mknode(N_VAR, -1, -1);
  nop[n] = lookvar(nm);
  return n;
}

static int expect(int t, char *what) {
  if (tok != t) die("syntax error, expected", what);
  nexttok();
  return 0;
}

static int skipnl(void) {
  while (tok == T_NL) nexttok();
  return 0;
}

/* 引数の並び。) まで */
static int arglist(int *cnt) {
  int head;
  int tail;
  int n;
  int a;
  head = -1;
  tail = -1;
  n = 0;
  if (tok == ')') { *cnt = 0; return -1; }
  while (1) {
    a = mknode(N_ARGS, expr(0), -1);
    if (head < 0) head = a;
    else nr[tail] = a;
    tail = a;
    n = n + 1;
    if (tok != ',') break;
    nexttok();
    skipnl();
  }
  *cnt = n;
  return head;
}

static int isere(int t) { return t == T_ERE; }

static int primary(void) {
  int n;
  int a;
  int cnt;
  if (tok == T_NUM) {
    n = mknode(N_NUM, -1, -1);
    nnum[n] = tnum;
    nexttok();
    return n;
  }
  if (tok == T_STR) {
    n = mknode(N_STR, -1, -1);
    nstr[n] = dupstr(tstr);
    nexttok();
    return n;
  }
  if (isere(tok)) {
    n = mknode(N_RE, -1, -1);
    nstr[n] = dupstr(tstr);
    nexttok();
    return n;
  }
  if (tok == '$') {
    nexttok();
    a = primary();
    n = mknode(N_FIELD, a, -1);
    return n;
  }
  if (tok == '(') {
    nexttok();
    a = expr(0);
    if (tok == ',') {
      /* (i, j) in arr */
      int head;
      int tail;
      int b;
      head = mknode(N_ARGS, a, -1);
      tail = head;
      while (tok == ',') {
        nexttok();
        skipnl();
        b = mknode(N_ARGS, expr(0), -1);
        nr[tail] = b;
        tail = b;
      }
      expect(')', ")");
      if (tok == T_IN) {
        nexttok();
        if (tok != T_NAME) die("expected array name after 'in'", 0);
        n = mknode(N_IN, head, mkvarnode(tstr));
        nexttok();
        return n;
      }
      /* `in` が続かないなら **print の並び** である ——
       * `print (a, b)` は a と b を並べて出す形で，autoconf の生成する
       * awk に現れる。print 以外の所に来たら実行時に拒む */
      n = mknode(N_GROUP, head, -1);
      nop[n] = 1;
      return n;
    }
    expect(')', ")");
    n = mknode(N_GROUP, a, -1);
    return n;
  }
  if (tok == T_INCR || tok == T_DECR) {
    int d;
    d = (tok == T_INCR) ? 1 : -1;
    nexttok();
    a = primary();
    n = mknode(N_INCDEC, a, -1);
    nop[n] = d;
    nnum[n] = 0;                /* 前置 */
    return n;
  }
  if (tok == T_BUILTIN) {
    int b;
    b = tval;
    nexttok();
    n = mknode(N_BUILTIN, -1, -1);
    nop[n] = b;
    if (tok == '(') {
      nexttok();
      skipnl();
      nl[n] = arglist(&cnt);
      nnum[n] = (double)cnt;
      expect(')', ")");
    } else {
      if (b != B_LENGTH) die("builtin needs arguments", 0);
      nl[n] = -1;
      nnum[n] = 0;
    }
    return n;
  }
  if (tok == T_FUNCNAME) {
    int f;
    f = addfun(tstr);
    nexttok();
    expect('(', "(");
    skipnl();
    n = mknode(N_CALL, -1, -1);
    nop[n] = f;
    nl[n] = arglist(&cnt);
    nnum[n] = (double)cnt;
    expect(')', ")");
    return n;
  }
  if (tok == T_GETLINE) {
    int v;
    nexttok();
    v = -1;
    if (tok == T_NAME || tok == '$') {
      if (tok == T_NAME) { v = mkvarnode(tstr); nexttok(); }
      else { nexttok(); v = mknode(N_FIELD, primary(), -1); }
    }
    n = mknode(N_GETLINE, v, -1);
    nop[n] = 0;
    if (tok == '<') {
      nexttok();
      nr[n] = expr(1);
      nop[n] = 1;
    }
    return n;
  }
  if (tok == T_NAME) {
    char nm[MAXSTR];
    strcpy(nm, tstr);
    nexttok();
    if (tok == '[') {
      int head;
      int tail;
      int b;
      nexttok();
      head = mknode(N_ARGS, expr(0), -1);
      tail = head;
      while (tok == ',') {
        nexttok();
        skipnl();
        b = mknode(N_ARGS, expr(0), -1);
        nr[tail] = b;
        tail = b;
      }
      expect(']', "]");
      n = mknode(N_INDEX, mkvarnode(nm), head);
      return n;
    }
    return mkvarnode(nm);
  }
  die("syntax error in expression", 0);
  return -1;
}

static int postfix(void) {
  int a;
  int n;
  a = primary();
  while (tok == T_INCR || tok == T_DECR) {
    int t;
    t = ntyp[a];
    if (t != N_VAR && t != N_LOCAL && t != N_FIELD && t != N_INDEX) break;
    n = mknode(N_INCDEC, a, -1);
    nop[n] = (tok == T_INCR) ? 1 : -1;
    nnum[n] = 1;                /* 後置 */
    nexttok();
    a = n;
  }
  return a;
}

static int powexp(void) {
  int a;
  int b;
  int n;
  a = postfix();
  if (tok == '^') {
    nexttok();
    /* 右結合。**単項マイナスより強い** (-2^2 は -4) */
    if (tok == '-') { nexttok(); b = mknode(N_NEG, powexp(), -1); }
    else b = powexp();
    n = mknode(N_ARITH, a, b);
    nop[n] = '^';
    return n;
  }
  return a;
}

static int unary(void) {
  int n;
  if (tok == '!') { nexttok(); return mknode(N_NOT, unary(), -1); }
  if (tok == '-') { nexttok(); n = mknode(N_NEG, unary(), -1); return n; }
  if (tok == '+') { nexttok(); return unary(); }
  return powexp();
}

static int mulexp(void) {
  int a;
  int n;
  a = unary();
  while (tok == '*' || tok == '/' || tok == '%') {
    int o;
    o = tok;
    nexttok();
    skipnl();
    n = mknode(N_ARITH, a, unary());
    nop[n] = o;
    a = n;
  }
  return a;
}

static int addexp(void) {
  int a;
  int n;
  a = mulexp();
  while (tok == '+' || tok == '-') {
    int o;
    o = tok;
    nexttok();
    skipnl();
    n = mknode(N_ARITH, a, mulexp());
    nop[n] = o;
    a = n;
  }
  return a;
}

/* 連結。**次の字句が式を始められるなら繋ぐ** —— + と - は足し算なので
 * 繋がない。ここが awk の構文でいちばん滑りやすい所である */
static int startsexpr(int t) {
  return t == T_NUM || t == T_STR || t == T_ERE || t == T_NAME
      || t == T_FUNCNAME || t == T_BUILTIN || t == '$' || t == '('
      || t == '!' || t == T_INCR || t == T_DECR;
}

static int catexp(int nogt) {
  int a;
  int n;
  a = addexp();
  while (startsexpr(tok)) {
    n = mknode(N_CAT, a, addexp());
    a = n;
  }
  return a;
}

static int relexp(int nogt) {
  int a;
  int o;
  int n;
  a = catexp(nogt);
  o = 0;
  if (tok == '<' || tok == T_LE || tok == T_NE || tok == T_EQ || tok == T_GE) o = tok;
  else if (tok == '>' && !nogt) o = tok;
  if (!o) return a;
  nexttok();
  skipnl();
  n = mknode(N_CMP, a, catexp(nogt));
  nop[n] = o;
  return n;
}

static int matchexp(int nogt) {
  int a;
  int n;
  a = relexp(nogt);
  while (tok == '~' || tok == T_NOMATCH) {
    int o;
    o = (tok == '~') ? 1 : 0;
    nexttok();
    skipnl();
    n = mknode(N_MATCH, a, relexp(nogt));
    nop[n] = o;
    a = n;
  }
  return a;
}

static int inexp(int nogt) {
  int a;
  int n;
  a = matchexp(nogt);
  while (tok == T_IN) {
    nexttok();
    if (tok != T_NAME) die("expected array name after 'in'", 0);
    n = mknode(N_IN, mknode(N_ARGS, a, -1), mkvarnode(tstr));
    nexttok();
    a = n;
  }
  return a;
}

static int andexp(int nogt) {
  int a;
  int n;
  a = inexp(nogt);
  while (tok == T_ANDAND) {
    nexttok();
    skipnl();
    n = mknode(N_AND, a, inexp(nogt));
    a = n;
  }
  return a;
}

static int orexp(int nogt) {
  int a;
  int n;
  a = andexp(nogt);
  while (tok == T_OROR) {
    nexttok();
    skipnl();
    n = mknode(N_OR, a, andexp(nogt));
    a = n;
  }
  return a;
}

static int ternary(int nogt) {
  int a;
  int b;
  int c;
  int n;
  a = orexp(nogt);
  if (tok == '?') {
    nexttok();
    skipnl();
    b = ternary(nogt);
    skipnl();
    expect(':', ":");
    skipnl();
    c = ternary(nogt);
    n = mknode(N_COND, a, b);
    nx[n] = c;
    return n;
  }
  return a;
}

static int islvalue(int n) {
  int t;
  t = ntyp[n];
  return t == N_VAR || t == N_LOCAL || t == N_FIELD || t == N_INDEX;
}

static int expr(int nogt) {
  int a;
  int n;
  int o;
  a = ternary(nogt);
  if (islvalue(a) && (tok == '=' || tok == T_ADDA || tok == T_SUBA
                      || tok == T_MULA || tok == T_DIVA || tok == T_MODA
                      || tok == T_POWA)) {
    o = tok;
    nexttok();
    skipnl();
    n = mknode(N_ASSIGN, a, expr(nogt));
    if (o == '=') nop[n] = '=';
    else if (o == T_ADDA) nop[n] = '+';
    else if (o == T_SUBA) nop[n] = '-';
    else if (o == T_MULA) nop[n] = '*';
    else if (o == T_DIVA) nop[n] = '/';
    else if (o == T_MODA) nop[n] = '%';
    else nop[n] = '^';
    return n;
  }
  return a;
}

/* 文の終わり */
static int endstmt(void) {
  while (tok == ';' || tok == T_NL) nexttok();
  return 0;
}

static int stmtlist(void) {
  int head;
  int tail;
  int s;
  int n;
  head = -1;
  tail = -1;
  skipnl();
  while (tok != '}' && tok != T_EOF) {
    s = stmt();
    if (s >= 0) {
      n = mknode(S_LIST, s, -1);
      if (head < 0) head = n;
      else nr[tail] = n;
      tail = n;
    }
    skipnl();
  }
  return head;
}

static int simpleprint(int isprintf) {
  int n;
  int cnt;
  int head;
  int tail;
  int a;
  n = mknode(isprintf ? S_PRINTF : S_PRINT, -1, -1);
  head = -1;
  tail = -1;
  cnt = 0;
  if (tok != T_NL && tok != ';' && tok != '}' && tok != T_EOF
      && tok != '>' && tok != T_APPEND) {
    while (1) {
      a = mknode(N_ARGS, expr(1), -1);
      if (head < 0) head = a;
      else nr[tail] = a;
      tail = a;
      cnt = cnt + 1;
      if (tok != ',') break;
      nexttok();
      skipnl();
    }
  }
  /* `print (a, b)` は括弧の中が並びである。単なる括弧 (nop == 0) は
   * そのままでよい */
  if (cnt == 1 && ntyp[nl[head]] == N_GROUP && nop[nl[head]] == 1) {
    head = nl[nl[head]];
    cnt = nargsof(head);
  }
  nl[n] = head;
  nnum[n] = (double)cnt;
  nop[n] = 0;
  if (tok == '>' || tok == T_APPEND) {
    nop[n] = (tok == '>') ? 1 : 2;
    nexttok();
    nx[n] = expr(1);
  }
  return n;
}

static int stmt(void) {
  int n;
  int a;
  int b;
  int c;
  if (tok == ';') { nexttok(); return -1; }
  if (tok == '{') {
    nexttok();
    n = stmtlist();
    expect('}', "}");
    return n < 0 ? mknode(S_NOP, -1, -1) : n;
  }
  if (tok == T_IF) {
    nexttok();
    expect('(', "(");
    a = expr(0);
    expect(')', ")");
    skipnl();
    b = stmt();
    n = mknode(S_IF, a, b);
    /* else は改行と ; を跨いでよい */
    {
      int save;
      save = spos;
      while (tok == T_NL || tok == ';') nexttok();
      if (tok == T_ELSE) {
        nexttok();
        skipnl();
        nx[n] = stmt();
      } else {
        /* else が無かった。読み過ぎた改行は捨ててよい */
        (void)save;
      }
    }
    return n;
  }
  if (tok == T_WHILE) {
    nexttok();
    expect('(', "(");
    a = expr(0);
    expect(')', ")");
    skipnl();
    if (tok == ';') { nexttok(); b = mknode(S_NOP, -1, -1); }
    else b = stmt();
    n = mknode(S_WHILE, a, b);
    return n;
  }
  if (tok == T_DO) {
    nexttok();
    skipnl();
    b = stmt();
    skipnl();
    if (tok != T_WHILE) die("expected 'while' after 'do'", 0);
    nexttok();
    expect('(', "(");
    a = expr(0);
    expect(')', ")");
    n = mknode(S_DO, a, b);
    endstmt();
    return n;
  }
  if (tok == T_FOR) {
    nexttok();
    expect('(', "(");
    if (tok == '(') {
      /* for ((i,j) in a) は受けない */
      die("for ((i,j) in a) is not supported", 0);
    }
    if (tok == T_NAME) {
      /* for (k in arr) か，普通の for か */
      int save;
      char nm[MAXSTR];
      strcpy(nm, tstr);
      save = spos;
      nexttok();
      if (tok == T_IN) {
        nexttok();
        if (tok != T_NAME) die("expected array name after 'in'", 0);
        a = mkvarnode(nm);
        b = mkvarnode(tstr);
        nexttok();
        expect(')', ")");
        skipnl();
        n = mknode(S_FORIN, a, b);
        nx[n] = stmt();
        return n;
      }
      /* 普通の for。名前をもう一度読ませるために巻き戻す */
      spos = save;
      lasttok = T_NAME;
      prevoperand = 1;
      strcpy(tstr, nm);
      tok = T_NAME;
    }
    a = (tok == ';') ? -1 : expr(0);
    expect(';', ";");
    skipnl();
    b = (tok == ';') ? -1 : expr(0);
    expect(';', ";");
    skipnl();
    c = (tok == ')') ? -1 : expr(0);
    expect(')', ")");
    skipnl();
    n = mknode(S_FOR, a, b);
    nx[n] = c;
    if (tok == ';') { nexttok(); ny[n] = mknode(S_NOP, -1, -1); }
    else ny[n] = stmt();
    return n;
  }
  if (tok == T_PRINT || tok == T_PRINTF) {
    int isp;
    isp = (tok == T_PRINTF);
    nexttok();
    n = simpleprint(isp);
    endstmt();
    return n;
  }
  if (tok == T_NEXT) { nexttok(); endstmt(); return mknode(S_NEXT, -1, -1); }
  if (tok == T_BREAK) { nexttok(); endstmt(); return mknode(S_BREAK, -1, -1); }
  if (tok == T_CONTINUE) { nexttok(); endstmt(); return mknode(S_CONT, -1, -1); }
  if (tok == T_EXIT) {
    nexttok();
    n = mknode(S_EXIT, -1, -1);
    if (tok != T_NL && tok != ';' && tok != '}' && tok != T_EOF) nl[n] = expr(0);
    endstmt();
    return n;
  }
  if (tok == T_RETURN) {
    nexttok();
    n = mknode(S_RETURN, -1, -1);
    if (tok != T_NL && tok != ';' && tok != '}' && tok != T_EOF) nl[n] = expr(0);
    endstmt();
    return n;
  }
  if (tok == T_DELETE) {
    nexttok();
    if (tok != T_NAME) die("expected array name after 'delete'", 0);
    {
      char nm[MAXSTR];
      strcpy(nm, tstr);
      nexttok();
      n = mknode(S_DELETE, mkvarnode(nm), -1);
      if (tok == '[') {
        int head;
        int tail;
        int e;
        nexttok();
        head = mknode(N_ARGS, expr(0), -1);
        tail = head;
        while (tok == ',') {
          nexttok();
          e = mknode(N_ARGS, expr(0), -1);
          nr[tail] = e;
          tail = e;
        }
        expect(']', "]");
        nr[n] = head;
      }
    }
    endstmt();
    return n;
  }
  n = mknode(S_EXPR, expr(0), -1);
  endstmt();
  return n;
}

static int parsefunc(void) {
  int f;
  int i;
  char nm[MAXSTR];
  nexttok();
  if (tok != T_NAME && tok != T_FUNCNAME) die("expected function name", 0);
  strcpy(nm, tstr);
  nexttok();
  expect('(', "(");
  f = addfun(nm);
  curfun = f;
  fnpn[f] = 0;
  skipnl();
  while (tok != ')') {
    if (tok != T_NAME) die("expected parameter name", 0);
    if (fnpn[f] >= NPARAM) die("too many parameters", 0);
    fpname[f][fnpn[f]] = dupstr(tstr);
    fnpn[f] = fnpn[f] + 1;
    nexttok();
    if (tok == ',') { nexttok(); skipnl(); }
  }
  nexttok();
  skipnl();
  expect('{', "{");
  fbody[f] = stmtlist();
  expect('}', "}");
  curfun = -1;
  for (i = 0; i < 0; i = i + 1) { }
  return f;
}

static int parseprog(void) {
  int p;
  int act;
  nexttok();
  while (tok != T_EOF) {
    skipnl();
    if (tok == T_EOF) break;
    if (tok == T_FUNCTION) { parsefunc(); endstmt(); continue; }
    if (nrule >= NRULE) die("too many rules", 0);
    if (tok == T_BEGIN) {
      nexttok();
      skipnl();
      expect('{', "{");
      act = stmtlist();
      expect('}', "}");
      rkind[nrule] = R_BEGIN;
      rpat[nrule] = -1;
      ract[nrule] = act;
      nrule = nrule + 1;
      endstmt();
      continue;
    }
    if (tok == T_END) {
      nexttok();
      skipnl();
      expect('{', "{");
      act = stmtlist();
      expect('}', "}");
      rkind[nrule] = R_END;
      rpat[nrule] = -1;
      ract[nrule] = act;
      nrule = nrule + 1;
      endstmt();
      continue;
    }
    p = -1;
    rkind[nrule] = R_PAT;
    rpat2[nrule] = -1;
    if (tok != '{') {
      p = expr(0);
      if (tok == ',') {
        nexttok();
        skipnl();
        rpat2[nrule] = expr(0);
        rkind[nrule] = R_RANGE;
      }
    }
    rpat[nrule] = p;
    ractive[nrule] = 0;
    if (tok == '{') {
      nexttok();
      act = stmtlist();
      expect('}', "}");
    } else {
      act = -1;                 /* 既定の動作は print $0 */
    }
    ract[nrule] = act;
    nrule = nrule + 1;
    endstmt();
  }
  return 0;
}

/* ================= 実行 ================= */

#define X_NORMAL 0
#define X_BREAK 1
#define X_CONT 2
#define X_NEXT 3
#define X_EXIT 4
#define X_RETURN 5

static int xflow;
static int exitcode;
static double retval_num;
static char *retval_str;
static int retval_typ;

static int eval(int n);
static int exec(int n);
static int arridof(int n);
static int mkkey(int args, char *out, int max);
static int assignto(int n, int vi);

/* 出力の宛先 */
static char *outname[NOUTF];
static FILE *outfp[NOUTF];
static int noutf;

static FILE *getout(char *nm, int append) {
  int i;
  for (i = 0; i < noutf; i = i + 1) {
    if (strcmp(outname[i], nm) == 0) return outfp[i];
  }
  if (noutf >= NOUTF) die("too many output files", 0);
  if (strcmp(nm, "/dev/stdout") == 0 || strcmp(nm, "-") == 0) {
    outfp[noutf] = stdout;
  } else if (strcmp(nm, "/dev/stderr") == 0) {
    outfp[noutf] = stderr;
  } else {
    outfp[noutf] = fopen(nm, append ? "a" : "w");
    if (outfp[noutf] == 0) die("cannot open for writing", nm);
  }
  outname[noutf] = dupstr(nm);
  noutf = noutf + 1;
  return outfp[noutf - 1];
}

/* 入力の宛先 (getline < file) */
static char *inname[NINF];
static FILE *infp[NINF];
static int ninf;

static FILE *getin(char *nm) {
  int i;
  for (i = 0; i < ninf; i = i + 1) {
    if (strcmp(inname[i], nm) == 0) return infp[i];
  }
  if (ninf >= NINF) die("too many input files", 0);
  if (strcmp(nm, "-") == 0 || strcmp(nm, "/dev/stdin") == 0) infp[ninf] = stdin;
  else infp[ninf] = fopen(nm, "r");
  inname[ninf] = dupstr(nm);
  ninf = ninf + 1;
  return infp[ninf - 1];
}

static int closef(char *nm) {
  int i;
  for (i = 0; i < noutf; i = i + 1) {
    if (strcmp(outname[i], nm) == 0) {
      if (outfp[i] != stdout && outfp[i] != stderr) fclose(outfp[i]);
      free(outname[i]);
      outname[i] = outname[noutf - 1];
      outfp[i] = outfp[noutf - 1];
      noutf = noutf - 1;
      return 0;
    }
  }
  for (i = 0; i < ninf; i = i + 1) {
    if (strcmp(inname[i], nm) == 0) {
      if (infp[i] && infp[i] != stdin) fclose(infp[i]);
      free(inname[i]);
      inname[i] = inname[ninf - 1];
      infp[i] = infp[ninf - 1];
      ninf = ninf - 1;
      return 0;
    }
  }
  return -1;
}

/* ---- 主入力 ---- */
static char **avfiles;
static int navfiles;
/* **実体のある operand を 1 つでも開いたか。** operand が変数の代入
 * だけだったとき (awk '...' v=ok) は，file を 1 つも読んでいないので
 * 標準入力を読む —— POSIX はそう定める。navfiles を見るだけだと
 * 「operand はあった」で標準入力を諦めてしまう */
static int sawfile;
static int avidx;
static FILE *mainfp;
static int maindone;

static int assignarg(char *s) {
  char *eq;
  char nm[256];
  int n;
  eq = strchr(s, '=');
  if (eq == 0) return 0;
  n = (int)(eq - s);
  if (n <= 0 || n >= 256) return 0;
  memcpy(nm, s, (size_t)n);
  nm[n] = 0;
  if (!idalpha((unsigned char)nm[0])) return 0;
  {
    int i;
    for (i = 1; i < n; i = i + 1) {
      if (!idalpha((unsigned char)nm[i]) && !iddigit((unsigned char)nm[i])) return 0;
    }
  }
  setvstr(lookvar(nm), eq + 1, 1);
  return 1;
}

static int rsval(void) {
  if (vstr[SV_RS] && vstr[SV_RS][0]) return (unsigned char)vstr[SV_RS][0];
  if (vstr[SV_RS] && vstr[SV_RS][0] == 0) return '\n';
  return '\n';
}

/* 1 本の記録を読む。読めたら 1 */
static int readrec(FILE *f, char *buf, int max) {
  int c;
  int n;
  int rs;
  if (f == 0) return 0;
  rs = rsval();
  n = 0;
  c = fgetc(f);
  if (c < 0) return 0;
  while (c >= 0 && c != rs) {
    if (n < max - 1) { buf[n] = (char)c; n = n + 1; }
    c = fgetc(f);
  }
  buf[n] = 0;
  return 1;
}

static int nextmain(char *buf, int max) {
  while (1) {
    if (mainfp == 0) {
      if (avidx >= navfiles) {
        if (maindone) return 0;
        maindone = 1;
        if (!sawfile) {
          mainfp = stdin;
          setvstr(SV_FILENAME, "", 0);
          vtyp[SV_FNR] = V_NUM;
          vnum[SV_FNR] = 0;
          continue;
        }
        return 0;
      }
      if (assignarg(avfiles[avidx])) { avidx = avidx + 1; continue; }
      if (strcmp(avfiles[avidx], "-") == 0) mainfp = stdin;
      else {
        mainfp = fopen(avfiles[avidx], "r");
        if (mainfp == 0) die("cannot open", avfiles[avidx]);
      }
      sawfile = 1;
      setvstr(SV_FILENAME, avfiles[avidx], 0);
      vtyp[SV_FNR] = V_NUM;
      vnum[SV_FNR] = 0;
      avidx = avidx + 1;
      maindone = 1;
    }
    if (readrec(mainfp, buf, max)) {
      vtyp[SV_NR] = V_NUM;
      vnum[SV_NR] = vnum[SV_NR] + 1;
      vtyp[SV_FNR] = V_NUM;
      vnum[SV_FNR] = vnum[SV_FNR] + 1;
      return 1;
    }
    if (mainfp && mainfp != stdin) fclose(mainfp);
    mainfp = 0;
    if (avidx >= navfiles) return 0;
    maindone = 0;
  }
}

/* ---- 書式の引数を並べ替える ---- */
#define NFARG 64
static char *fa_s[NFARG];
static double fa_n[NFARG];
static int fa_i[NFARG];

/* スタックに積んだ値を awkfmt1 が読める形にする。**awk の値の二面性は
 * ここで畳む** —— 書式の側は「字」と「数」しか知らない */
static int fmtargs(int base, int argn) {
  int i;
  if (argn > NFARG) argn = NFARG;
  for (i = 0; i < argn; i = i + 1) {
    fa_s[i] = tostr(base + i);
    fa_n[i] = tonum(base + i);
    fa_i[i] = (vtp[base + i] == V_NUM);
  }
  return argn;
}

/* ---- 代入先 ---- */
static int assignto(int n, int vi) {
  int t;
  t = ntyp[n];
  if (t == N_VAR) {
    int v;
    v = nop[n];
    if (vtp[vi] == V_NUM) {
      setvnum(v, vnm[vi]);
    } else {
      setvstr(v, tostr(vi), vtp[vi] == V_SN);
      if (vtp[vi] == V_SN) { vtyp[v] = V_SN; vnum[v] = vnm[vi]; }
    }
    if (v == SV_NF) {
      int k;
      int w;
      w = (int)svnum(SV_NF);
      if (w < 0) w = 0;
      for (k = w + 1; k <= nfld; k = k + 1) { if (fld[k]) { free(fld[k]); fld[k] = 0; } }
      for (k = nfld + 1; k <= w; k = k + 1) { if (fld[k]) free(fld[k]); fld[k] = dupstr(""); }
      nfld = w;
      rebuild();
    }
    return 0;
  }
  if (t == N_LOCAL) {
    int k;
    k = nop[n];
    if (vtp[vi] == V_NUM) {
      fnpar[fdepth][k] = V_NUM;
      fnpnum[fdepth][k] = vnm[vi];
      if (fnpstr[fdepth][k]) { free(fnpstr[fdepth][k]); fnpstr[fdepth][k] = 0; }
    } else {
      char *s;
      s = dupstr(tostr(vi));
      if (fnpstr[fdepth][k]) free(fnpstr[fdepth][k]);
      fnpstr[fdepth][k] = s;
      fnpar[fdepth][k] = vtp[vi] == V_SN ? V_SN : V_STR;
      fnpnum[fdepth][k] = vnm[vi];
    }
    return 0;
  }
  if (t == N_FIELD) {
    int idx;
    eval(nl[n]);
    idx = (int)tonum(sp - 1);
    sp = sp - 1;
    setfield(idx, tostr(vi));
    return 0;
  }
  if (t == N_INDEX) {
    int arr;
    char key[MAXSTR];
    int e;
    arr = arridof(nl[n]);
    mkkey(nr[n], key, MAXSTR);
    e = findel(arr, key, 1);
    if (vtp[vi] == V_NUM) {
      if (estr[e]) { free(estr[e]); estr[e] = 0; }
      etyp[e] = V_NUM;
      enm[e] = vnm[vi];
    } else {
      char *d;
      /* **先に複製する** —— 右辺がこの要素そのものを指していることが
       * ある (`a[i] = a[i]`)。欄で同じ誤りを踏んだ (setfield) */
      d = dupstr(tostr(vi));
      if (estr[e]) free(estr[e]);
      estr[e] = d;
      etyp[e] = vtp[vi] == V_SN ? V_SN : V_STR;
      enm[e] = vnm[vi];
    }
    return 0;
  }
  die("assignment to non-lvalue", 0);
  return 0;
}

/* 変数節から配列番号を得る */
static int arridof(int n) {
  if (ntyp[n] == N_VAR) {
    int v;
    v = nop[n];
    if (varrid[v] < 0) { varrid[v] = narr; narr = narr + 1; }
    vkind[v] = 2;
    return varrid[v];
  }
  if (ntyp[n] == N_LOCAL) {
    int k;
    k = nop[n];
    if (fnparr[fdepth][k] < 0) { fnparr[fdepth][k] = narr; narr = narr + 1; }
    return fnparr[fdepth][k];
  }
  die("not an array", 0);
  return 0;
}

/* 添字の並びを SUBSEP で繋いだ鍵にする */
static int mkkey(int args, char *out, int max) {
  int n;
  int a;
  char *ss;
  n = 0;
  ss = subsepval();
  for (a = args; a >= 0; a = nr[a]) {
    char *s;
    int l;
    eval(nl[a]);
    s = tostr(sp - 1);
    l = (int)strlen(s);
    if (n + l + 1 >= max) die("subscript too long", 0);
    if (n > 0) {
      int sl;
      sl = (int)strlen(ss);
      if (n + sl + l + 1 >= max) die("subscript too long", 0);
      memcpy(out + n, ss, (size_t)sl);
      n = n + sl;
    }
    memcpy(out + n, s, (size_t)l);
    n = n + l;
    sp = sp - 1;
  }
  out[n] = 0;
  return n;
}

/* ---- 置換 (sub / gsub) ---- */
static int dosub(int h, char *rep, char *in, char *out, int global, int max) {
  char *p;
  char *mb;
  char *me;
  int n;
  int cnt;
  char *lastend;
  n = 0;
  cnt = 0;
  p = in;
  lastend = 0;
  while (re_search_from(h, in, p, &mb, &me)) {
    char *q;
    if (re_overrun()) die("regexp too complex", 0);
    if (mb == me && lastend == mb) {
      /* 空に合った直後は 1 字進める */
      if (*p == 0) break;
      if (n < max - 1) { out[n] = *p; n = n + 1; }
      p = p + 1;
      continue;
    }
    while (p < mb) {
      if (n < max - 1) { out[n] = *p; n = n + 1; }
      p = p + 1;
    }
    q = rep;
    while (*q) {
      if (*q == '\\' && q[1] == '&') { if (n < max - 1) { out[n] = '&'; n = n + 1; } q = q + 2; continue; }
      if (*q == '\\' && q[1] == '\\') { if (n < max - 1) { out[n] = '\\'; n = n + 1; } q = q + 2; continue; }
      if (*q == '&') {
        char *r;
        for (r = mb; r < me; r = r + 1) { if (n < max - 1) { out[n] = *r; n = n + 1; } }
        q = q + 1;
        continue;
      }
      if (n < max - 1) { out[n] = *q; n = n + 1; }
      q = q + 1;
    }
    cnt = cnt + 1;
    lastend = me;
    if (me == mb) {
      if (*p == 0) { p = me; break; }
      if (n < max - 1) { out[n] = *p; n = n + 1; }
      p = p + 1;
    } else {
      p = me;
    }
    if (!global) break;
  }
  while (*p) { if (n < max - 1) { out[n] = *p; n = n + 1; } p = p + 1; }
  out[n] = 0;
  return cnt;
}

/* ---- 組込み ---- */
static int nargsof(int args) {
  int n;
  int a;
  n = 0;
  for (a = args; a >= 0; a = nr[a]) n = n + 1;
  return n;
}

static int argnode(int args, int k) {
  int a;
  int i;
  i = 0;
  for (a = args; a >= 0; a = nr[a]) {
    if (i == k) return nl[a];
    i = i + 1;
  }
  return -1;
}

static unsigned long rndstate;

static int dobuiltin(int n) {
  int b;
  int args;
  int na;
  b = nop[n];
  args = nl[n];
  na = nargsof(args);
  if (b == B_LENGTH) {
    if (na == 0) { pushnum((double)strlen(rec)); return 0; }
    {
      int a;
      a = argnode(args, 0);
      if ((ntyp[a] == N_VAR && vkind[nop[a]] == 2)
          || (ntyp[a] == N_LOCAL && fnparr[fdepth][nop[a]] >= 0)) {
        int arr;
        int i;
        int c;
        arr = arridof(a);
        c = 0;
        for (i = 0; i < ecnt; i = i + 1) {
          if (earr[i] == arr && !edel[i]) c = c + 1;
        }
        pushnum((double)c);
        return 0;
      }
      eval(a);
      {
        double d;
        d = (double)strlen(tostr(sp - 1));
        sp = sp - 1;
        pushnum(d);
      }
      return 0;
    }
  }
  if (b == B_SUBSTR) {
    char *s;
    double m;
    double l;
    int i;
    int j;
    int sl;
    char buf[MAXSTR];
    if (na < 2) die("substr needs 2 or 3 arguments", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    sl = (int)strlen(s);
    eval(argnode(args, 1));
    m = tonum(sp - 1);
    sp = sp - 1;
    if (na >= 3) {
      eval(argnode(args, 2));
      l = tonum(sp - 1);
      sp = sp - 1;
    } else {
      l = (double)sl + 1.0;
    }
    /* POSIX: 位置は 1 起点。範囲外は切り詰める */
    {
      double st;
      double en;
      st = m;
      en = m + l;
      if (st < 1) st = 1;
      if (en > (double)sl + 1) en = (double)sl + 1;
      if (en <= st) { sp = sp - 1; pushstr(""); return 0; }
      i = (int)st;
      j = (int)en;
    }
    sp = sp - 1;
    if (j - i >= MAXSTR) die("substr too long", 0);
    memcpy(buf, s + i - 1, (size_t)(j - i));
    buf[j - i] = 0;
    pushstr(acopy(buf));
    return 0;
  }
  if (b == B_INDEX) {
    char *s;
    char *t;
    char *q;
    if (na < 2) die("index needs 2 arguments", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    eval(argnode(args, 1));
    t = tostr(sp - 1);
    q = strstr(s, t);
    sp = sp - 2;
    pushnum(q ? (double)(q - s + 1) : 0.0);
    return 0;
  }
  if (b == B_SPLIT) {
    char *s;
    char *fs;
    int arr;
    int cnt;
    char *sbuf;
    sbuf = bufsplitin;
    if (na < 2) die("split needs 2 or 3 arguments", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    if ((int)strlen(s) >= LINEMAX) die("split: string too long", 0);
    strcpy(sbuf, s);
    sp = sp - 1;
    arr = arridof(argnode(args, 1));
    delarr(arr);
    if (na >= 3) {
      int a3;
      a3 = argnode(args, 2);
      if (ntyp[a3] == N_RE) {
        fs = nstr[a3];
        if (fs[0] && fs[1] == 0) { /* 1 字なら字そのもの */ }
      } else {
        eval(a3);
        fs = tostr(sp - 1);
        sp = sp - 1;
      }
    } else {
      fs = fsval();
    }
    cnt = splitrec(sbuf, fs, arr);
    pushnum((double)cnt);
    return 0;
  }
  if (b == B_SUB || b == B_GSUB) {
    int h;
    char *rep;
    char *in;
    char *out;
    int cnt;
    int target;
    int a0;
    out = bufsub;
    if (na < 2) die("sub needs 2 or 3 arguments", 0);
    a0 = argnode(args, 0);
    if (ntyp[a0] == N_RE) h = getre(nstr[a0], 1);
    else {
      eval(a0);
      h = getre(tostr(sp - 1), 1);
      sp = sp - 1;
    }
    eval(argnode(args, 1));
    rep = tostr(sp - 1);
    target = (na >= 3) ? argnode(args, 2) : -1;
    if (target < 0) in = rec;
    else {
      eval(target);
      in = tostr(sp - 1);
    }
    cnt = dosub(h, rep, in, out, b == B_GSUB, LINEMAX);
    if (target < 0) sp = sp - 1;
    else sp = sp - 2;
    if (cnt > 0) {
      if (target < 0) {
        setfield(0, out);
      } else {
        pushstr(acopy(out));
        assignto(target, sp - 1);
        sp = sp - 1;
      }
    }
    pushnum((double)cnt);
    return 0;
  }
  if (b == B_MATCH) {
    int h;
    char *s;
    char *mb;
    char *me;
    int a1;
    if (na < 2) die("match needs 2 arguments", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    a1 = argnode(args, 1);
    if (ntyp[a1] == N_RE) h = getre(nstr[a1], 1);
    else {
      eval(a1);
      h = getre(tostr(sp - 1), 1);
      sp = sp - 1;
    }
    if (re_search(h, s, &mb, &me)) {
      if (re_overrun()) die("regexp too complex", 0);
      setvnum(SV_RSTART, (double)(mb - s + 1));
      setvnum(SV_RLENGTH, (double)(me - mb));
      sp = sp - 1;
      pushnum((double)(mb - s + 1));
    } else {
      if (re_overrun()) die("regexp too complex", 0);
      setvnum(SV_RSTART, 0);
      setvnum(SV_RLENGTH, -1);
      sp = sp - 1;
      pushnum(0);
    }
    return 0;
  }
  if (b == B_SPRINTF) {
    char *f;
    char *out;
    int base;
    int i;
    out = bufout;
    if (na < 1) die("sprintf needs a format", 0);
    base = sp;
    for (i = 0; i < na; i = i + 1) eval(argnode(args, i));
    f = tostr(base);
    awk_fmt(f, fmtargs(base + 1, na - 1), fa_s, fa_n, fa_i, out, LINEMAX);
    sp = base;
    pushstr(acopy(out));
    return 0;
  }
  if (b == B_TOUPPER || b == B_TOLOWER) {
    char buf[MAXSTR];
    char *s;
    int i;
    if (na < 1) die("toupper/tolower needs an argument", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    for (i = 0; s[i] && i < MAXSTR - 1; i = i + 1) {
      int c;
      c = (unsigned char)s[i];
      if (b == B_TOUPPER) { if (c >= 'a' && c <= 'z') c = c - ('a' - 'A'); }
      else { if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A'); }
      buf[i] = (char)c;
    }
    buf[i] = 0;
    sp = sp - 1;
    pushstr(acopy(buf));
    return 0;
  }
  if (b == B_INT) {
    double d;
    if (na < 1) die("int needs an argument", 0);
    eval(argnode(args, 0));
    d = tonum(sp - 1);
    sp = sp - 1;
    if (d < 0) d = -(double)(long long)(-d);
    else d = (double)(long long)d;
    pushnum(d);
    return 0;
  }
  if (b == B_CLOSE) {
    char *s;
    int r;
    if (na < 1) die("close needs an argument", 0);
    eval(argnode(args, 0));
    s = tostr(sp - 1);
    r = closef(s);
    sp = sp - 1;
    pushnum((double)r);
    return 0;
  }
  if (b == B_SRAND) {
    unsigned long old;
    old = rndstate;
    if (na >= 1) {
      eval(argnode(args, 0));
      rndstate = (unsigned long)tonum(sp - 1);
      sp = sp - 1;
    } else {
      rndstate = 1;
    }
    pushnum((double)old);
    return 0;
  }
  if (b == B_RAND) {
    rndstate = rndstate * 1103515245ul + 12345ul;
    pushnum((double)((rndstate >> 16) & 0x7fffu) / 32768.0);
    return 0;
  }
  die("unknown builtin", 0);
  return 0;
}

/* ---- 関数呼出 ---- */
static int docall(int n) {
  int f;
  int args;
  int na;
  int i;
  int base;
  int saved;
  f = nop[n];
  args = nl[n];
  na = nargsof(args);
  if (fbody[f] < 0) die("call to undefined function", fname[f]);
  if (fdepth + 1 >= NFRAME) die("too deep recursion", 0);
  if (na > fnpn[f]) die("too many arguments to function", fname[f]);
  base = sp;
  /* 引数を先に値へ。**配列は番号で渡す** */
  {
    int pass[NPARAM];
    int isarr[NPARAM];
    for (i = 0; i < fnpn[f]; i = i + 1) { pass[i] = -1; isarr[i] = -1; }
    for (i = 0; i < na; i = i + 1) {
      int a;
      a = argnode(args, i);
      if (ntyp[a] == N_VAR && (vkind[nop[a]] == 2 || vkind[nop[a]] == 0)) {
        isarr[i] = arridof(a);
        if (vkind[nop[a]] == 2) { pass[i] = -1; continue; }
        vkind[nop[a]] = 0;      /* まだスカラとも配列とも決まっていない */
        pushuninit();
        pass[i] = sp - 1;
        continue;
      }
      if (ntyp[a] == N_LOCAL) {
        int k;
        k = nop[a];
        if (fnparr[fdepth][k] >= 0 && fnpar[fdepth][k] == V_UNINIT) {
          isarr[i] = fnparr[fdepth][k];
          pushuninit();
          pass[i] = sp - 1;
          continue;
        }
      }
      eval(a);
      pass[i] = sp - 1;
    }
    saved = fdepth;
    fdepth = fdepth + 1;
    for (i = 0; i < fnpn[f]; i = i + 1) {
      fnpar[fdepth][i] = V_UNINIT;
      fnpnum[fdepth][i] = 0;
      fnpstr[fdepth][i] = 0;
      fnparr[fdepth][i] = -1;
    }
    for (i = 0; i < na; i = i + 1) {
      if (isarr[i] >= 0) fnparr[fdepth][i] = isarr[i];
      if (pass[i] >= 0 && vtp[pass[i]] != V_UNINIT) {
        if (vtp[pass[i]] == V_NUM) {
          fnpar[fdepth][i] = V_NUM;
          fnpnum[fdepth][i] = vnm[pass[i]];
        } else {
          fnpar[fdepth][i] = vtp[pass[i]];
          fnpnum[fdepth][i] = vnm[pass[i]];
          fnpstr[fdepth][i] = dupstr(tostr(pass[i]));
        }
      }
    }
    /* 余った仮引数は局所変数。局所の配列にも番号を配る */
    for (i = na; i < fnpn[f]; i = i + 1) {
      fnparr[fdepth][i] = narr;
      narr = narr + 1;
    }
    sp = base;
    retval_typ = V_UNINIT;
    retval_num = 0;
    retval_str = 0;
    exec(fbody[f]);
    if (xflow == X_RETURN) xflow = X_NORMAL;
    /* 局所を片づける */
    for (i = 0; i < fnpn[f]; i = i + 1) {
      if (fnpstr[fdepth][i]) { free(fnpstr[fdepth][i]); fnpstr[fdepth][i] = 0; }
    }
    for (i = na; i < fnpn[f]; i = i + 1) delarr(fnparr[fdepth][i]);
    fdepth = saved;
  }
  if (retval_typ == V_NUM) pushnum(retval_num);
  else if (retval_typ == V_UNINIT) pushuninit();
  else {
    pushstr(acopy(retval_str ? retval_str : ""));
    if (retval_typ == V_SN) { vtp[sp - 1] = V_SN; vnm[sp - 1] = retval_num; }
  }
  if (retval_str) { free(retval_str); retval_str = 0; }
  return 0;
}

/* ---- 比較 ---- */
static int isnumeric(int i) {
  return vtp[i] == V_NUM || vtp[i] == V_SN || vtp[i] == V_UNINIT;
}

static int docmp(int a, int b) {
  if (isnumeric(a) && isnumeric(b)) {
    double x;
    double y;
    x = tonum(a);
    y = tonum(b);
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
  }
  {
    char *x;
    char *y;
    int r;
    x = tostr(a);
    y = tostr(b);
    r = strcmp(x, y);
    if (r < 0) return -1;
    if (r > 0) return 1;
    return 0;
  }
}

/* ---- getline ---- */
static int dogetline(int n) {
  char *buf;
  int v;
  int r;
  buf = bufgetline;
  v = nl[n];
  if (nop[n] == 1) {
    FILE *f;
    char *nm;
    eval(nr[n]);
    nm = tostr(sp - 1);
    f = getin(nm);
    sp = sp - 1;
    if (f == 0) { pushnum(-1); return 0; }
    r = readrec(f, buf, LINEMAX);
    if (!r) { pushnum(0); return 0; }
    if (v < 0) {
      setrec(buf);
    } else {
      pushin(buf);
      assignto(v, sp - 1);
      sp = sp - 1;
    }
    pushnum(1);
    return 0;
  }
  r = nextmain(buf, LINEMAX);
  if (!r) { pushnum(0); return 0; }
  if (v < 0) {
    setrec(buf);
  } else {
    pushin(buf);
    assignto(v, sp - 1);
    sp = sp - 1;
  }
  pushnum(1);
  return 0;
}

/* ---- 式 ---- */
static int eval(int n) {
  int t;
  if (n < 0) { pushuninit(); return 0; }
  t = ntyp[n];
  if (t == N_NUM) { pushnum(nnum[n]); return 0; }
  if (t == N_STR) { pushstr(nstr[n]); return 0; }
  if (t == N_GROUP) {
    if (nop[n] == 1) die("(a, b) is only allowed before 'in' or after print", 0);
    return eval(nl[n]);
  }
  if (t == N_RE) {
    /* 素の正規表現は $0 との照合である */
    int h;
    char *mb;
    char *me;
    h = getre(nstr[n], 1);
    {
      int r;
      r = re_search(h, rec, &mb, &me);
      if (re_overrun()) die("regexp too complex", 0);
      pushnum(r ? 1.0 : 0.0);
    }
    return 0;
  }
  if (t == N_VAR) {
    int v;
    v = nop[n];
    if (vtyp[v] == V_NUM) pushnum(vnum[v]);
    else if (vtyp[v] == V_UNINIT) pushuninit();
    else {
      pushstr(vstr[v] ? vstr[v] : "");
      if (vtyp[v] == V_SN) { vtp[sp - 1] = V_SN; vnm[sp - 1] = vnum[v]; }
    }
    return 0;
  }
  if (t == N_LOCAL) {
    int k;
    k = nop[n];
    if (fnpar[fdepth][k] == V_NUM) pushnum(fnpnum[fdepth][k]);
    else if (fnpar[fdepth][k] == V_UNINIT) pushuninit();
    else {
      pushstr(fnpstr[fdepth][k] ? fnpstr[fdepth][k] : "");
      if (fnpar[fdepth][k] == V_SN) { vtp[sp - 1] = V_SN; vnm[sp - 1] = fnpnum[fdepth][k]; }
    }
    return 0;
  }
  if (t == N_FIELD) {
    int i;
    eval(nl[n]);
    i = (int)tonum(sp - 1);
    sp = sp - 1;
    pushin(getfield(i));
    return 0;
  }
  if (t == N_INDEX) {
    int arr;
    char key[MAXSTR];
    int e;
    arr = arridof(nl[n]);
    mkkey(nr[n], key, MAXSTR);
    e = findel(arr, key, 1);
    if (etyp[e] == V_NUM) pushnum(enm[e]);
    else if (etyp[e] == V_UNINIT) pushuninit();
    else {
      pushstr(estr[e] ? estr[e] : "");
      if (etyp[e] == V_SN) { vtp[sp - 1] = V_SN; vnm[sp - 1] = enm[e]; }
    }
    return 0;
  }
  if (t == N_ASSIGN) {
    if (nop[n] == '=') {
      eval(nr[n]);
      assignto(nl[n], sp - 1);
      return 0;
    }
    eval(nl[n]);
    eval(nr[n]);
    {
      double x;
      double y;
      double r;
      x = tonum(sp - 2);
      y = tonum(sp - 1);
      sp = sp - 2;
      r = 0;
      if (nop[n] == '+') r = x + y;
      else if (nop[n] == '-') r = x - y;
      else if (nop[n] == '*') r = x * y;
      else if (nop[n] == '/') { if (y == 0) die("division by zero", 0); r = x / y; }
      else if (nop[n] == '%') {
        long long a;
        long long b;
        if (y == 0) die("division by zero", 0);
        a = (long long)x;
        b = (long long)y;
        r = (double)(a % b);
      } else {
        int i;
        int e;
        r = 1;
        e = (int)y;
        if (e < 0) { for (i = 0; i < -e; i = i + 1) r = r / x; }
        else { for (i = 0; i < e; i = i + 1) r = r * x; }
      }
      pushnum(r);
      assignto(nl[n], sp - 1);
    }
    return 0;
  }
  if (t == N_COND) {
    int c;
    eval(nl[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    return eval(c ? nr[n] : nx[n]);
  }
  if (t == N_OR) {
    int c;
    eval(nl[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    if (c) { pushnum(1); return 0; }
    eval(nr[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    pushnum(c ? 1.0 : 0.0);
    return 0;
  }
  if (t == N_AND) {
    int c;
    eval(nl[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    if (!c) { pushnum(0); return 0; }
    eval(nr[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    pushnum(c ? 1.0 : 0.0);
    return 0;
  }
  if (t == N_NOT) {
    int c;
    eval(nl[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    pushnum(c ? 0.0 : 1.0);
    return 0;
  }
  if (t == N_MATCH) {
    int h;
    char *s;
    char *mb;
    char *me;
    int r;
    eval(nl[n]);
    s = tostr(sp - 1);
    if (ntyp[nr[n]] == N_RE) h = getre(nstr[nr[n]], 1);
    else {
      eval(nr[n]);
      h = getre(tostr(sp - 1), 1);
      sp = sp - 1;
    }
    r = re_search(h, s, &mb, &me);
    if (re_overrun()) die("regexp too complex", 0);
    sp = sp - 1;
    if (!nop[n]) r = !r;
    pushnum(r ? 1.0 : 0.0);
    return 0;
  }
  if (t == N_CMP) {
    int c;
    int o;
    int r;
    eval(nl[n]);
    eval(nr[n]);
    c = docmp(sp - 2, sp - 1);
    sp = sp - 2;
    o = nop[n];
    r = 0;
    if (o == '<') r = c < 0;
    else if (o == T_LE) r = c <= 0;
    else if (o == '>') r = c > 0;
    else if (o == T_GE) r = c >= 0;
    else if (o == T_EQ) r = c == 0;
    else r = c != 0;
    pushnum(r ? 1.0 : 0.0);
    return 0;
  }
  if (t == N_CAT) {
    char *a;
    char *b;
    char *o;
    int la;
    int lb;
    eval(nl[n]);
    eval(nr[n]);
    a = tostr(sp - 2);
    b = tostr(sp - 1);
    la = (int)strlen(a);
    lb = (int)strlen(b);
    o = aalloc(la + lb);
    memcpy(o, a, (size_t)la);
    memcpy(o + la, b, (size_t)lb);
    sp = sp - 2;
    pushstr(o);
    return 0;
  }
  if (t == N_ARITH) {
    double x;
    double y;
    double r;
    eval(nl[n]);
    eval(nr[n]);
    x = tonum(sp - 2);
    y = tonum(sp - 1);
    sp = sp - 2;
    r = 0;
    if (nop[n] == '+') r = x + y;
    else if (nop[n] == '-') r = x - y;
    else if (nop[n] == '*') r = x * y;
    else if (nop[n] == '/') { if (y == 0) die("division by zero", 0); r = x / y; }
    else if (nop[n] == '%') {
      long long a;
      long long b;
      if (y == 0) die("division by zero", 0);
      a = (long long)x;
      b = (long long)y;
      r = (double)(a % b);
    } else {
      int i;
      int e;
      r = 1;
      e = (int)y;
      if (e < 0) { for (i = 0; i < -e; i = i + 1) r = r / x; }
      else { for (i = 0; i < e; i = i + 1) r = r * x; }
    }
    pushnum(r);
    return 0;
  }
  if (t == N_NEG) {
    double d;
    eval(nl[n]);
    d = tonum(sp - 1);
    sp = sp - 1;
    pushnum(-d);
    return 0;
  }
  if (t == N_INCDEC) {
    double d;
    eval(nl[n]);
    d = tonum(sp - 1);
    sp = sp - 1;
    pushnum(d + (double)nop[n]);
    assignto(nl[n], sp - 1);
    if (nnum[n] != 0) {         /* 後置は元の値 */
      sp = sp - 1;
      pushnum(d);
    }
    return 0;
  }
  if (t == N_IN) {
    int arr;
    char key[MAXSTR];
    int e;
    arr = arridof(nr[n]);
    mkkey(nl[n], key, MAXSTR);
    e = findel(arr, key, 0);
    pushnum(e >= 0 ? 1.0 : 0.0);
    return 0;
  }
  if (t == N_CALL) return docall(n);
  if (t == N_BUILTIN) return dobuiltin(n);
  if (t == N_GETLINE) return dogetline(n);
  die("internal: unknown expression", 0);
  return 0;
}

/* ---- 文 ---- */
static int doprint(int n) {
  FILE *f;
  int args;
  int i;
  int na;
  int base;
  f = stdout;
  if (nop[n]) {
    char *nm;
    eval(nx[n]);
    nm = tostr(sp - 1);
    f = getout(nm, nop[n] == 2);
    sp = sp - 1;
  }
  args = nl[n];
  na = nargsof(args);
  base = sp;
  if (ntyp[n] == S_PRINT) {
    if (na == 0) {
      fputs(rec, f);
    } else {
      for (i = 0; i < na; i = i + 1) {
        eval(argnode(args, i));
        if (i > 0) fputs(ofsval(), f);
        fputs(tostro(sp - 1), f);
      }
    }
    fputs(orsval(), f);
    sp = base;
    return 0;
  }
  {
    char *out;
    char *fmt;
    out = bufout;
    if (na < 1) die("printf needs a format", 0);
    for (i = 0; i < na; i = i + 1) eval(argnode(args, i));
    fmt = tostr(base);
    awk_fmt(fmt, fmtargs(base + 1, na - 1), fa_s, fa_n, fa_i, out, LINEMAX);
    fputs(out, f);
    sp = base;
  }
  return 0;
}

static int exec(int n) {
  int t;
  int mark;
  if (n < 0) return 0;
  t = ntyp[n];
  if (t == S_LIST) {
    int a;
    for (a = n; a >= 0; a = nr[a]) {
      exec(nl[a]);
      if (xflow != X_NORMAL) return 0;
    }
    return 0;
  }
  mark = ap;
  if (t == S_NOP) return 0;
  if (t == S_EXPR) { eval(nl[n]); sp = sp - 1; ap = mark; return 0; }
  if (t == S_PRINT || t == S_PRINTF) { doprint(n); ap = mark; return 0; }
  if (t == S_IF) {
    int c;
    eval(nl[n]);
    c = tobool(sp - 1);
    sp = sp - 1;
    ap = mark;
    if (c) exec(nr[n]);
    else if (nx[n] >= 0) exec(nx[n]);
    return 0;
  }
  if (t == S_WHILE) {
    while (1) {
      int c;
      mark = ap;
      eval(nl[n]);
      c = tobool(sp - 1);
      sp = sp - 1;
      ap = mark;
      if (!c) break;
      exec(nr[n]);
      if (xflow == X_BREAK) { xflow = X_NORMAL; break; }
      if (xflow == X_CONT) xflow = X_NORMAL;
      if (xflow != X_NORMAL) return 0;
    }
    return 0;
  }
  if (t == S_DO) {
    while (1) {
      int c;
      exec(nr[n]);
      if (xflow == X_BREAK) { xflow = X_NORMAL; break; }
      if (xflow == X_CONT) xflow = X_NORMAL;
      if (xflow != X_NORMAL) return 0;
      mark = ap;
      eval(nl[n]);
      c = tobool(sp - 1);
      sp = sp - 1;
      ap = mark;
      if (!c) break;
    }
    return 0;
  }
  if (t == S_FOR) {
    if (nl[n] >= 0) { eval(nl[n]); sp = sp - 1; ap = mark; }
    while (1) {
      int c;
      mark = ap;
      if (nr[n] >= 0) {
        eval(nr[n]);
        c = tobool(sp - 1);
        sp = sp - 1;
      } else {
        c = 1;
      }
      ap = mark;
      if (!c) break;
      exec(ny[n]);
      if (xflow == X_BREAK) { xflow = X_NORMAL; break; }
      if (xflow == X_CONT) xflow = X_NORMAL;
      if (xflow != X_NORMAL) return 0;
      if (nx[n] >= 0) { mark = ap; eval(nx[n]); sp = sp - 1; ap = mark; }
    }
    return 0;
  }
  if (t == S_FORIN) {
    int arr;
    int i;
    int lim;
    arr = arridof(nr[n]);
    lim = ecnt;
    for (i = 0; i < lim; i = i + 1) {
      if (earr[i] != arr || edel[i]) continue;
      mark = ap;
      pushstr(acopy(ekey[i]));
      vtp[sp - 1] = looksnum(vst[sp - 1]) ? V_SN : V_STR;
      if (vtp[sp - 1] == V_SN) vnm[sp - 1] = strtod(vst[sp - 1], (char **)0);
      assignto(nl[n], sp - 1);
      sp = sp - 1;
      ap = mark;
      exec(nx[n]);
      if (xflow == X_BREAK) { xflow = X_NORMAL; break; }
      if (xflow == X_CONT) xflow = X_NORMAL;
      if (xflow != X_NORMAL) return 0;
    }
    return 0;
  }
  if (t == S_NEXT) { xflow = X_NEXT; return 0; }
  if (t == S_BREAK) { xflow = X_BREAK; return 0; }
  if (t == S_CONT) { xflow = X_CONT; return 0; }
  if (t == S_EXIT) {
    if (nl[n] >= 0) {
      eval(nl[n]);
      exitcode = (int)tonum(sp - 1);
      sp = sp - 1;
    }
    ap = mark;
    xflow = X_EXIT;
    return 0;
  }
  if (t == S_RETURN) {
    retval_typ = V_UNINIT;
    retval_num = 0;
    retval_str = 0;
    if (nl[n] >= 0) {
      eval(nl[n]);
      if (vtp[sp - 1] == V_NUM) { retval_typ = V_NUM; retval_num = vnm[sp - 1]; }
      else if (vtp[sp - 1] == V_UNINIT) retval_typ = V_UNINIT;
      else {
        retval_typ = vtp[sp - 1];
        retval_num = vnm[sp - 1];
        retval_str = dupstr(tostr(sp - 1));
      }
      sp = sp - 1;
    }
    ap = mark;
    xflow = X_RETURN;
    return 0;
  }
  if (t == S_DELETE) {
    int arr;
    arr = arridof(nl[n]);
    if (nr[n] < 0) delarr(arr);
    else {
      char key[MAXSTR];
      mkkey(nr[n], key, MAXSTR);
      delel(arr, key);
    }
    ap = mark;
    return 0;
  }
  die("internal: unknown statement", 0);
  return 0;
}

/* ================= 走らせる ================= */

static int runrules(void) {
  int i;
  for (i = 0; i < nrule; i = i + 1) {
    int hit;
    if (rkind[i] == R_BEGIN || rkind[i] == R_END) continue;
    hit = 0;
    if (rkind[i] == R_RANGE) {
      if (!ractive[i]) {
        int mark;
        mark = ap;
        eval(rpat[i]);
        hit = tobool(sp - 1);
        sp = sp - 1;
        ap = mark;
        if (hit) {
          ractive[i] = 1;
          {
            int m2;
            m2 = ap;
            eval(rpat2[i]);
            if (tobool(sp - 1)) ractive[i] = 0;
            sp = sp - 1;
            ap = m2;
          }
        }
      } else {
        hit = 1;
        {
          int m2;
          m2 = ap;
          eval(rpat2[i]);
          if (tobool(sp - 1)) ractive[i] = 0;
          sp = sp - 1;
          ap = m2;
        }
      }
    } else if (rpat[i] < 0) {
      hit = 1;
    } else {
      int mark;
      mark = ap;
      eval(rpat[i]);
      hit = tobool(sp - 1);
      sp = sp - 1;
      ap = mark;
    }
    if (!hit) continue;
    if (ract[i] < 0) {
      fputs(rec, stdout);
      fputs(orsval(), stdout);
      continue;
    }
    exec(ract[i]);
    if (xflow == X_NEXT) { xflow = X_NORMAL; return 0; }
    if (xflow == X_EXIT) return 1;
    xflow = X_NORMAL;
  }
  return 0;
}

static int needinput(void) {
  int i;
  for (i = 0; i < nrule; i = i + 1) {
    if (rkind[i] != R_BEGIN) return 1;
  }
  return 0;
}

static int initvars(void) {
  int i;
  char *nm[NSPECIAL];
  nm[SV_NR] = "NR";
  nm[SV_NF] = "NF";
  nm[SV_FS] = "FS";
  nm[SV_OFS] = "OFS";
  nm[SV_ORS] = "ORS";
  nm[SV_RS] = "RS";
  nm[SV_FILENAME] = "FILENAME";
  nm[SV_SUBSEP] = "SUBSEP";
  nm[SV_RSTART] = "RSTART";
  nm[SV_RLENGTH] = "RLENGTH";
  nm[SV_CONVFMT] = "CONVFMT";
  nm[SV_OFMT] = "OFMT";
  nm[SV_FNR] = "FNR";
  for (i = 0; i < NSPECIAL; i = i + 1) {
    if (lookvar(nm[i]) != i) die("internal: special variable order", nm[i]);
  }
  setvnum(SV_NR, 0);
  setvnum(SV_NF, 0);
  setvstr(SV_FS, " ", 0);
  setvstr(SV_OFS, " ", 0);
  setvstr(SV_ORS, "\n", 0);
  setvstr(SV_RS, "\n", 0);
  setvstr(SV_FILENAME, "", 0);
  setvstr(SV_SUBSEP, "\034", 0);
  setvnum(SV_RSTART, 0);
  setvnum(SV_RLENGTH, -1);
  setvstr(SV_CONVFMT, "%.6g", 0);
  setvstr(SV_OFMT, "%.6g", 0);
  setvnum(SV_FNR, 0);
  return 0;
}

static int addsrc(char *s) {
  int n;
  n = (int)strlen(s);
  if ((int)strlen(src) + n + 2 >= MAXSRC) die("program too long", 0);
  strcat(src, s);
  strcat(src, "\n");
  return 0;
}

static int readfileinto(char *path) {
  FILE *f;
  int c;
  int n;
  f = fopen(path, "r");
  if (f == 0) die("cannot open program file", path);
  n = (int)strlen(src);
  while ((c = fgetc(f)) >= 0) {
    if (n + 2 >= MAXSRC) die("program too long", 0);
    src[n] = (char)c;
    n = n + 1;
  }
  src[n] = '\n';
  src[n + 1] = 0;
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  int i;
  int haveprog;
  char *buf;
  buf = bufgetline;
  progname = argv[0];
  awk_fmtinit();
  for (i = 0; i < NHASH; i = i + 1) hbkt[i] = -1;
  curfun = -1;
  rndstate = 1;
  initvars();
  haveprog = 0;
  i = 1;
  while (i < argc) {
    char *a;
    a = argv[i];
    if (strcmp(a, "--") == 0) { i = i + 1; break; }
    if (a[0] != '-' || a[1] == 0) break;
    if (a[1] == 'f') {
      char *p;
      if (a[2]) p = a + 2;
      else { i = i + 1; if (i >= argc) die("-f needs a file", 0); p = argv[i]; }
      readfileinto(p);
      haveprog = 1;
      i = i + 1;
      continue;
    }
    if (a[1] == 'v') {
      char *p;
      if (a[2]) p = a + 2;
      else { i = i + 1; if (i >= argc) die("-v needs var=value", 0); p = argv[i]; }
      if (!assignarg(p)) die("bad -v assignment", p);
      i = i + 1;
      continue;
    }
    if (a[1] == 'F') {
      char *p;
      char fsb[64];
      if (a[2]) p = a + 2;
      else { i = i + 1; if (i >= argc) die("-F needs a separator", 0); p = argv[i]; }
      /* -F '\t' の形をほどく */
      if (p[0] == '\\' && p[1] && p[2] == 0) {
        fsb[0] = (char)escof((unsigned char)p[1]);
        fsb[1] = 0;
        p = fsb;
      }
      if (p[0] == 't' && p[1] == 0) { fsb[0] = '\t'; fsb[1] = 0; p = fsb; }
      setvstr(SV_FS, p, 0);
      i = i + 1;
      continue;
    }
    die("unknown option", a);
  }
  if (!haveprog) {
    if (i >= argc) die("usage: awk [-F fs] [-v var=val] 'prog' [file ...]", 0);
    addsrc(argv[i]);
    i = i + 1;
  }
  avfiles = argv + i;
  navfiles = argc - i;
  avidx = 0;
  parseprog();
  /* BEGIN */
  for (i = 0; i < nrule; i = i + 1) {
    if (rkind[i] != R_BEGIN) continue;
    exec(ract[i]);
    if (xflow == X_EXIT) break;
    xflow = X_NORMAL;
  }
  if (xflow != X_EXIT && needinput()) {
    while (nextmain(buf, LINEMAX)) {
      setrec(buf);
      ap = 0;
      if (runrules()) break;
    }
  }
  if (xflow == X_EXIT) xflow = X_NORMAL;
  for (i = 0; i < nrule; i = i + 1) {
    if (rkind[i] != R_END) continue;
    exec(ract[i]);
    if (xflow == X_EXIT) break;
    xflow = X_NORMAL;
  }
  fflush(stdout);
  for (i = 0; i < noutf; i = i + 1) {
    if (outfp[i] != stdout && outfp[i] != stderr) fclose(outfp[i]);
  }
  return exitcode;
}
