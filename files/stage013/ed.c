/* ed.c --- 行エディタ (Stage 13 第 2 部)
 *
 * 設計は docs/stage013-tools.md 5 章。ゲスト内でソースを直す手段として，
 * ed の作法にならった行エディタを作る。画面制御を要さないので UART の
 * 上でそのまま動き，スクリプト (標準入力に流したコマンド列) でも
 * 対話でも同じように使える。
 *
 * 誤りはすべて `?` の 1 行で表す (ed の作法)。詳しい理由は出さない。
 * 置換は正規表現ではなく **文字列そのまま** の一致である (5.3)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NLINE 4096              /* 収められる行数 */
#define LMAX  512               /* 1 行の最大長 (NUL を含む) */

char *buf[4096];                /* 行の実体 (malloc)。NLINE 個。行 k は buf[k-1] */
int nline;                      /* 行数 */
int dot;                        /* 現在行 (1 起点。0 = 空) */
int dirty;                      /* 最後に書いてから変えたか */
int warned;                     /* q の警告を出したか */
char fname[64];                 /* 既定のファイル名 */

char line[512];                 /* 読み込み中のコマンド行。LMAX バイト */
char *cp;                       /* line 内の解析位置 */
int a1;                         /* 範囲の始め行 */
int a2;                         /* 範囲の終り行 */
int naddr;                      /* 明示されたアドレスの個数 */

/* 誤りは ? の 1 行で表す */
int err(void) {
  fputs("?\n", stdout);
  return 0;
}

char *dupstr(char *s) {
  char *p;
  p = malloc(strlen(s) + 1);
  if (p != NULL) strcpy(p, s);
  return p;
}

/* stdin から 1 行読み，行末の改行を落とす。
 * 入力の終わり (EOT または EOF) なら 0 を返す (docs/stage013-tools.md 3.4) */
int rdline(char *s, int n) {
  char *p;
  int k;
  if (fgets(s, n, stdin) == NULL) return 0;
  p = strchr(s, 4);
  if (p != NULL) { *p = 0; return 0; }
  k = (int)strlen(s);
  while (k > 0 && (s[k - 1] == '\n' || s[k - 1] == '\r')) {
    k = k - 1;
    s[k] = 0;
  }
  return 1;
}

/* ---- 行の出し入れ ---- */

/* 行 p の後ろへ s を挿入する (p は 0..nline)。溢れたら 0 */
int insline(int p, char *s) {
  int i;
  if (nline >= NLINE) return 0;
  for (i = nline; i > p; i--) buf[i] = buf[i - 1];
  buf[p] = s;
  nline = nline + 1;
  return 1;
}

/* 行 a..b を消す */
void delrange(int a, int b) {
  int i;
  int n;
  n = b - a + 1;
  for (i = a; i <= b; i++) free(buf[i - 1]);
  for (i = b; i < nline; i++) buf[i - n] = buf[i];
  nline = nline - n;
}

/* 行 a..b を出す。num が真なら行番号を前置する */
void prange(int a, int b, int num) {
  int i;
  for (i = a; i <= b; i++) {
    if (num) printf("%d\t", i);
    fputs(buf[i - 1], stdout);
    putchar('\n');
  }
  dot = b;
}

/* ---- アドレスの解析 ---- */

void skipsp(void) {
  while (*cp == ' ' || *cp == '\t') cp++;
}

/* 数を読む。cp は数字の先頭にある */
int getnum(void) {
  int n;
  n = 0;
  while (*cp >= '0' && *cp <= '9') {
    n = n * 10 + (*cp - '0');
    cp++;
  }
  return n;
}

/* アドレスを 1 つ読む。明示されていれば 1 を返し *v へ入れる。
 * 受ける形は . $ 数 と，それらに続く +N / -N (N の既定は 1) */
int addr1(int *v) {
  int n;
  int got;
  int d;
  int sign;

  skipsp();
  n = dot;
  got = 0;
  if (*cp == '.') { cp++; got = 1; }
  else if (*cp == '$') { n = nline; cp++; got = 1; }
  else if (*cp >= '0' && *cp <= '9') { n = getnum(); got = 1; }
  for (;;) {
    skipsp();
    if (*cp == '+') sign = 1;
    else if (*cp == '-') sign = -1;
    else break;
    cp++;
    skipsp();
    if (*cp >= '0' && *cp <= '9') d = getnum();
    else d = 1;
    n = n + sign * d;
    got = 1;
  }
  *v = n;
  return got;
}

/* コマンドの前にある範囲を読む。既定は現在行 1 行，`,` だけなら 1,$ */
void getrange(void) {
  int v;
  naddr = 0;
  a1 = dot;
  a2 = dot;
  if (addr1(&v)) { a1 = v; a2 = v; naddr = 1; }
  skipsp();
  if (*cp == ',') {
    cp++;
    if (naddr == 0) a1 = 1;
    if (addr1(&v)) a2 = v;
    else a2 = nline;
    naddr = 2;
  }
}

/* a1..a2 が 1..nline に収まっているか */
int okrange(void) {
  return a1 >= 1 && a1 <= a2 && a2 <= nline;
}

/* ---- 入力モード (a / i / c) ---- */

/* 行 at の後ろから入力を足す。'.' だけの行で終わる。
 * 戻り値は 1 = 正常，0 = 入力の終わり，-1 = 誤り (溢れ) */
int input(int at) {
  char in[512];
  char *s;
  for (;;) {
    if (!rdline(in, LMAX)) return 0;
    if (in[0] == '.' && in[1] == 0) return 1;
    s = dupstr(in);
    if (s == NULL) return -1;
    if (!insline(at, s)) { free(s); return -1; }
    at = at + 1;
    dot = at;
    dirty = 1;
  }
}

/* ---- ファイル ---- */

/* a..b を name へ書く。書いたバイト数 (誤りは -1) */
int wfile(char *name, int a, int b) {
  FILE *f;
  int i;
  int n;
  f = fopen(name, "w");
  if (f == NULL) return -1;
  n = 0;
  for (i = a; i <= b; i++) {
    fputs(buf[i - 1], f);
    fputc('\n', f);
    n = n + (int)strlen(buf[i - 1]) + 1;
  }
  fclose(f);
  return n;
}

/* name を読み，行 at の後ろへ入れる。読んだバイト数 (誤りは -1)。
 * LMAX に収まらない行はそこで切れる (行エディタの限界として受け入れる) */
int rfile(char *name, int at) {
  FILE *f;
  char in[512];
  char *s;
  int n;
  int k;
  f = fopen(name, "r");
  if (f == NULL) return -1;
  n = 0;
  for (;;) {
    if (fgets(in, LMAX, f) == NULL) break;
    k = (int)strlen(in);
    n = n + k;
    while (k > 0 && (in[k - 1] == '\n' || in[k - 1] == '\r')) {
      k = k - 1;
      in[k] = 0;
    }
    s = dupstr(in);
    if (s == NULL) { fclose(f); return -1; }
    if (!insline(at, s)) { free(s); fclose(f); return -1; }
    at = at + 1;
    dot = at;
  }
  fclose(f);
  return n;
}

/* ---- 置換 ---- */

/* 行 k の old を rep へ換える。glob なら行内のすべて。
 * 戻り値は 1 = 換えた，0 = 一致なし，-1 = 溢れ */
int subst(int k, char *old, char *rep, int glob) {
  char out[512];
  char *s;
  char *p;
  char *q;
  int n;
  int lo;
  int lr;
  int hit;

  lo = (int)strlen(old);
  if (lo == 0) return -1;
  lr = (int)strlen(rep);
  s = buf[k - 1];
  n = 0;
  hit = 0;
  for (;;) {
    p = strstr(s, old);
    if (p == NULL) break;
    if (n + (int)(p - s) + lr >= LMAX) return -1;
    memcpy(out + n, s, (size_t)(p - s));
    n = n + (int)(p - s);
    memcpy(out + n, rep, (size_t)lr);
    n = n + lr;
    s = p + lo;
    hit = 1;
    if (!glob) break;
  }
  if (!hit) return 0;
  if (n + (int)strlen(s) >= LMAX) return -1;
  strcpy(out + n, s);
  q = dupstr(out);
  if (q == NULL) return -1;
  free(buf[k - 1]);
  buf[k - 1] = q;
  return 1;
}

/* ---- 本体 ---- */

int main(int argc, char **argv) {
  char *nm;
  char *old;
  char *rep;
  char *q;
  int c;
  int n;
  int k;
  int glob;
  int hit;
  char d;

  if (argc > 1) {
    if ((int)strlen(argv[1]) > 62) { err(); return 1; }
    strcpy(fname, argv[1]);
    n = rfile(fname, 0);
    if (n < 0) err();
    else printf("%d\n", n);
  }

  for (;;) {
    if (!rdline(line, LMAX)) return 0;
    cp = line;
    getrange();
    skipsp();
    c = *cp;
    if (c != 0) cp++;

    if (c == 0) {
      /* アドレスだけならそこへ移って出す。空行なら次の行 (ed の作法) */
      if (naddr == 0) {
        if (dot >= nline) { err(); continue; }
        prange(dot + 1, dot + 1, 0);
      } else {
        if (a2 < 1 || a2 > nline) { err(); continue; }
        prange(a2, a2, 0);
      }
    } else if (c == 'p' || c == 'n') {
      if (!okrange()) { err(); continue; }
      prange(a1, a2, c == 'n');
    } else if (c == 'a') {
      if (a2 < 0 || a2 > nline) { err(); continue; }
      n = input(a2);
      if (n < 0) { err(); continue; }
      if (n == 0) return 0;
    } else if (c == 'i') {
      if (a2 > nline) { err(); continue; }
      if (a2 < 1) k = 0;
      else k = a2 - 1;
      n = input(k);
      if (n < 0) { err(); continue; }
      if (n == 0) return 0;
    } else if (c == 'c') {
      if (!okrange()) { err(); continue; }
      delrange(a1, a2);
      dirty = 1;
      n = input(a1 - 1);
      if (n < 0) { err(); continue; }
      if (n == 0) return 0;
    } else if (c == 'd') {
      if (!okrange()) { err(); continue; }
      delrange(a1, a2);
      dirty = 1;
      dot = a1;
      if (dot > nline) dot = nline;
    } else if (c == '=') {
      if (naddr == 0) printf("%d\n", nline);
      else printf("%d\n", a2);
    } else if (c == 'w') {
      if (naddr == 0) { a1 = 1; a2 = nline; }
      skipsp();
      if (*cp != 0) nm = cp;
      else nm = fname;
      if (nm[0] == 0) { err(); continue; }
      if (a1 < 1) a1 = 1;
      if (a2 > nline) a2 = nline;
      n = wfile(nm, a1, a2);
      if (n < 0) { err(); continue; }
      if (fname[0] == 0 && (int)strlen(nm) < 63) strcpy(fname, nm);
      if (strcmp(nm, fname) == 0) dirty = 0;
      printf("%d\n", n);
    } else if (c == 'r') {
      skipsp();
      if (*cp != 0) nm = cp;
      else nm = fname;
      if (nm[0] == 0) { err(); continue; }
      if (naddr == 0) a2 = nline;
      if (a2 < 0 || a2 > nline) { err(); continue; }
      n = rfile(nm, a2);
      if (n < 0) { err(); continue; }
      dirty = 1;
      printf("%d\n", n);
    } else if (c == 's') {
      d = *cp;
      if (d == 0) { err(); continue; }
      cp++;
      old = cp;
      q = strchr(old, d);
      if (q == NULL) { err(); continue; }
      *q = 0;
      rep = q + 1;
      q = strchr(rep, d);
      glob = 0;
      if (q != NULL) {
        *q = 0;
        if (q[1] == 'g') glob = 1;
      }
      if (!okrange()) { err(); continue; }
      hit = 0;
      for (k = a1; k <= a2; k++) {
        n = subst(k, old, rep, glob);
        if (n < 0) { err(); hit = -1; break; }
        if (n > 0) { hit = 1; dot = k; dirty = 1; }
      }
      if (hit == 0) err();
    } else if (c == 'q') {
      if (dirty && !warned) { warned = 1; err(); continue; }
      return 0;
    } else if (c == 'Q') {
      return 0;
    } else {
      err();
    }
  }
}
