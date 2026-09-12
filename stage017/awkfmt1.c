/* awkfmt1.c --- awk の数と書式 (docs/stage017-gcc.md 5.8)
 *
 * 設計の意図は awkfmt1.h の註にある。数を十進へ寄せる所と，書式に
 * 値を並べる所だけが入っている。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "awkfmt1.h"

static int fmtint(unsigned long long v, int base, int up, char *out);

/* 浮動小数点の 3 つの形 (%e / %f / %g) は **libc に任せる**。
 *
 * 第 1 世代を書いたときは `libc22` が `%g` と `%e` を `%f` と同じに
 * 扱っていたので awk の側で持っていた。`libc23` がその穴を埋めた
 * (docs/stage017-gcc.md 6.3) ので，**写しを 2 つ持つ理由が消えた** ——
 * 同じ規則を書く場所が 2 つあると必ず片方だけ直す誤りが出る
 * (stage018-ext.md 3.2 / 5.1 と同じ形)。
 *
 * 桁寄せと半端の倒し方 (Dekker の手で積を正確に求めて偶数へ倒す) は
 * `libc23/posix/stdio.c` にある。 */
static int fpone(double v, int kind, int prec, int up, int alt, char *out) {
  char spec[16];
  int n;
  n = 0;
  spec[n] = '%'; n = n + 1;
  if (alt) { spec[n] = '#'; n = n + 1; }
  spec[n] = '.'; n = n + 1;
  spec[n] = '*'; n = n + 1;
  if (up) spec[n] = (char)(kind - ('a' - 'A'));
  else spec[n] = (char)kind;
  n = n + 1;
  spec[n] = 0;
  return sprintf(out, spec, prec, v);
}

static int fmte(double v, int prec, int up, char *out) {
  return fpone(v, 'e', prec, up, 0, out);
}

static int fmtf(double v, int prec, char *out) {
  return fpone(v, 'f', prec, 0, 0, out);
}

static int fmtg(double v, int prec, int up, int alt, char *out) {
  return fpone(v, 'g', prec, up, alt, out);
}

/* 整数を字にする (基数つき) */
static int fmtint(unsigned long long v, int base, int up, char *out) {
  char tmp[32];
  int n;
  int i;
  int d;
  n = 0;
  if (v == 0) { tmp[n] = '0'; n = 1; }
  while (v > 0) {
    d = (int)(v % (unsigned long long)base);
    v = v / (unsigned long long)base;
    if (d < 10) tmp[n] = (char)('0' + d);
    else tmp[n] = (char)((up ? 'A' : 'a') + d - 10);
    n = n + 1;
  }
  for (i = 0; i < n; i = i + 1) out[i] = tmp[n - 1 - i];
  out[n] = 0;
  return n;
}


/* 数を字にする。**整数なら整数の形**にするのが awk の規則で，
 * そうでなければ CONVFMT (出力なら OFMT) を通す */
int awk_numstr(double d, char *fmt, char *out) {
  unsigned long long u;
  int neg;
  int n;
  double t;
  t = d < 0 ? -d : d;
  /* 境は long long に収まる範囲。ここを 1e18 に切ると 2^62 が指数の
   * 形になってしまう */
  if (t < 9.2e18 && d == (double)(long long)d) {
    neg = d < 0;
    u = (unsigned long long)(neg ? -d : d);
    n = 0;
    if (neg) { out[0] = '-'; n = 1; }
    fmtint(u, 10, 0, out + n);
    return 0;
  }
  {
    char *f;
    int prec;
    int kind;
    int up;
    f = fmt;
    prec = 6;
    kind = 'g';
    up = 0;
    if (f[0] == '%') {
      char *q;
      q = f + 1;
      while (*q == '-' || *q == '+' || *q == ' ' || *q == '#' || *q == '0') q = q + 1;
      while (*q >= '0' && *q <= '9') q = q + 1;
      if (*q == '.') {
        q = q + 1;
        prec = 0;
        while (*q >= '0' && *q <= '9') { prec = prec * 10 + (*q - '0'); q = q + 1; }
      }
      if (*q == 'l') q = q + 1;
      if (*q) kind = *q;
    }
    if (kind == 'E' || kind == 'G') { up = 1; kind = kind + ('a' - 'A'); }
    if (kind == 'e') fmte(d, prec, up, out);
    else if (kind == 'f') fmtf(d, prec, out);
    else if (kind == 'd' || kind == 'i') fmtf(d, 0, out);
    else fmtg(d, prec, up, 0, out);
  }
  return 0;
}

/* いまは支度が要らない (浮動小数点の変換を libc に任せたため)。
 * 呼び手の形は残す —— 世代が変わって支度が要るようになったときに，
 * 呼ぶ場所を探し直さずに済む */
int awk_fmtinit(void) { return 0; }

int awk_fmt(char *fmt, int argc, char **as, double *an, int *aisnum,
            char *out, int outmax) {
  int n;
  char *p;
  int ai;
  char spec[64];
  char tmp[1024];
  n = 0;
  p = fmt;
  ai = 0;
  while (*p) {
    int flagminus;
    int flagzero;
    int flagplus;
    int flagspace;
    int flagalt;
    int width;
    int prec;
    int haveprec;
    int sl;
    int k;
    int pad;
    if (*p != '%') {
      if (n < outmax - 1) { out[n] = *p; n = n + 1; }
      p = p + 1;
      continue;
    }
    p = p + 1;
    if (*p == '%') {
      if (n < outmax - 1) { out[n] = '%'; n = n + 1; }
      p = p + 1;
      continue;
    }
    flagminus = 0; flagzero = 0; flagplus = 0; flagspace = 0; flagalt = 0;
    while (*p == '-' || *p == '0' || *p == '+' || *p == ' ' || *p == '#') {
      if (*p == '-') flagminus = 1;
      if (*p == '0') flagzero = 1;
      if (*p == '+') flagplus = 1;
      if (*p == ' ') flagspace = 1;
      if (*p == '#') flagalt = 1;
      p = p + 1;
    }
    width = 0;
    if (*p == '*') {
      if (ai < argc) { width = (int)an[ai]; ai = ai + 1; }
      p = p + 1;
      if (width < 0) { flagminus = 1; width = -width; }
    } else {
      while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p = p + 1; }
    }
    prec = 0;
    haveprec = 0;
    if (*p == '.') {
      p = p + 1;
      haveprec = 1;
      if (*p == '*') {
        if (ai < argc) { prec = (int)an[ai]; ai = ai + 1; }
        p = p + 1;
      } else {
        while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p = p + 1; }
      }
    }
    while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'q' || *p == 'j'
           || *p == 'z' || *p == 't') p = p + 1;
    if (*p == 0) break;
    k = *p;
    p = p + 1;
    tmp[0] = 0;
    if (k == 'c') {
      int ch;
      ch = 0;
      if (ai < argc) {
        if (aisnum[ai]) ch = (int)an[ai];
        else ch = (unsigned char)as[ai][0];
        ai = ai + 1;
      }
      if (ch == 0) { tmp[0] = 0; sl = 0; }
      else { tmp[0] = (char)ch; tmp[1] = 0; sl = 1; }
      /* 幅だけ効く */
      pad = width - sl;
      if (!flagminus) { while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; } }
      for (k = 0; k < sl; k = k + 1) { if (n < outmax - 1) { out[n] = tmp[k]; n = n + 1; } }
      if (flagminus) { while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; } }
      continue;
    }
    if (k == 's') {
      char *s;
      s = "";
      if (ai < argc) { s = as[ai]; ai = ai + 1; }
      sl = (int)strlen(s);
      if (haveprec && prec < sl) sl = prec;
      pad = width - sl;
      if (!flagminus) { while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; } }
      for (k = 0; k < sl; k = k + 1) { if (n < outmax - 1) { out[n] = s[k]; n = n + 1; } }
      if (flagminus) { while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; } }
      continue;
    }
    {
      double v;
      char body[1024];
      char sign[2];
      int bl;
      v = 0;
      if (ai < argc) { v = an[ai]; ai = ai + 1; }
      sign[0] = 0;
      sign[1] = 0;
      if (k == 'd' || k == 'i') {
        unsigned long long u;
        int neg;
        neg = v < 0;
        if (v < 0) v = -v;
        if (v >= 1.8e19) v = 0;
        u = (unsigned long long)v;
        fmtint(u, 10, 0, body);
        if (neg) sign[0] = '-';
        else if (flagplus) sign[0] = '+';
        else if (flagspace) sign[0] = ' ';
        if (haveprec) {
          int bl2;
          bl2 = (int)strlen(body);
          if (bl2 < prec) {
            char t2[1024];
            int j;
            for (j = 0; j < prec - bl2; j = j + 1) t2[j] = '0';
            strcpy(t2 + prec - bl2, body);
            strcpy(body, t2);
          }
          flagzero = 0;
        }
      } else if (k == 'o' || k == 'x' || k == 'X' || k == 'u') {
        unsigned long long u;
        int bs;
        if (v < 0) {
          long long sv;
          sv = (long long)v;
          u = (unsigned long long)sv;
        } else {
          u = (unsigned long long)v;
        }
        bs = 10;
        if (k == 'o') bs = 8;
        if (k == 'x' || k == 'X') bs = 16;
        fmtint(u, bs, k == 'X', body);
        if (flagalt && bs == 8 && body[0] != '0') {
          char t2[1024];
          t2[0] = '0';
          strcpy(t2 + 1, body);
          strcpy(body, t2);
        }
      } else if (k == 'e' || k == 'E') {
        if (!haveprec) prec = 6;
        fmte(v, prec, k == 'E', body);
        if (v >= 0 && flagplus) sign[0] = '+';
        else if (v >= 0 && flagspace) sign[0] = ' ';
      } else if (k == 'f' || k == 'F') {
        if (!haveprec) prec = 6;
        fmtf(v, prec, body);
        if (v >= 0 && flagplus) sign[0] = '+';
        else if (v >= 0 && flagspace) sign[0] = ' ';
      } else if (k == 'g' || k == 'G') {
        if (!haveprec) prec = 6;
        fmtg(v, prec, k == 'G', flagalt, body);
        if (v >= 0 && flagplus) sign[0] = '+';
        else if (v >= 0 && flagspace) sign[0] = ' ';
      } else {
        body[0] = (char)k;
        body[1] = 0;
      }
      bl = (int)strlen(body);
      if (sign[0] && body[0] == '-') { sign[0] = 0; }
      sl = bl + (sign[0] ? 1 : 0);
      pad = width - sl;
      if (!flagminus && !flagzero) {
        while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; }
      }
      if (sign[0]) { if (n < outmax - 1) { out[n] = sign[0]; n = n + 1; } }
      if (!flagminus && flagzero) {
        int st;
        st = 0;
        if (body[0] == '-') { if (n < outmax - 1) { out[n] = '-'; n = n + 1; } st = 1; }
        while (pad > 0) { if (n < outmax - 1) { out[n] = '0'; n = n + 1; } pad = pad - 1; }
        for (k = st; k < bl; k = k + 1) { if (n < outmax - 1) { out[n] = body[k]; n = n + 1; } }
      } else {
        for (k = 0; k < bl; k = k + 1) { if (n < outmax - 1) { out[n] = body[k]; n = n + 1; } }
      }
      if (flagminus) {
        while (pad > 0) { if (n < outmax - 1) { out[n] = ' '; n = n + 1; } pad = pad - 1; }
      }
    }
  }
  out[n] = 0;
  return n;
}

