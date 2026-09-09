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

static double p10[9];

static int fmtint(unsigned long long v, int base, int up, char *out);

static int initp10(void) {
  int i;
  p10[0] = 1e1;
  for (i = 1; i < 9; i = i + 1) p10[i] = p10[i - 1] * p10[i - 1];
  return 0;
}

/* 10^0 .. 10^22 は double で**正確に**表せる。ここまでは掛けても
 * 誤差が入らないので，桁寄せを 1 回の掛け算で済ませられる */
static double pw10[23];

static int initpw10(void) {
  int i;
  pw10[0] = 1.0;
  for (i = 1; i < 23; i = i + 1) pw10[i] = pw10[i - 1] * 10.0;
  return 0;
}

/* a * b の丸め誤差を正確に求める (Dekker)。**半端の判定に要る** ——
 * 2.45 は 2 進では 2.45 より僅かに大きいので %.1f は 2.5 になるべき
 * だが，2.45 * 10 を素直に計算すると丁度 24.5 に丸まって「半端」に
 * 見えてしまい，偶数丸めで 2.4 になる */
static double prod_err(double a, double b, double p) {
  double c;
  double ahi;
  double alo;
  double bhi;
  double blo;
  c = 134217729.0 * a;          /* 2^27 + 1 */
  ahi = c - (c - a);
  alo = a - ahi;
  c = 134217729.0 * b;
  bhi = c - (c - b);
  blo = b - bhi;
  return ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
}

/* v * 10^k を整数へ丸める。**半端は偶数へ** (ホストの printf と同じ) */
static unsigned long long scaleround(double v, int k) {
  double t;
  double err;
  double frac;
  unsigned long long n;
  err = 0;
  if (k >= 0 && k <= 22) {
    t = v * pw10[k];
    err = prod_err(v, pw10[k], t);
  } else if (k >= 0) {
    t = v;
    while (k > 22) { t = t * pw10[22]; k = k - 22; }
    t = t * pw10[k];
  } else {
    int m;
    m = -k;
    t = v;
    while (m > 22) { t = t / pw10[22]; m = m - 22; }
    t = t / pw10[m];
  }
  n = (unsigned long long)t;
  frac = (t - (double)n) + err;
  if (frac > 0.5) n = n + 1;
  else if (frac == 0.5) { if (n % 2 == 1) n = n + 1; }
  return n;
}

/* v (> 0) の上位 nsig 桁を digs に入れ，先頭桁の 10 の冪を *ex に返す */
static int digits(double v, int nsig, char *digs, int *ex) {
  int e;
  int i;
  int k;
  double w;
  unsigned long long n;
  unsigned long long lim;
  /* まず桁の位置を見当づける。ここの誤差は下で直す */
  e = 0;
  w = v;
  for (i = 8; i >= 0; i = i - 1) {
    while (w >= p10[i]) { w = w / p10[i]; e = e + (1 << i); }
  }
  for (i = 8; i >= 0; i = i - 1) {
    while (w * p10[i] < 10.0) { w = w * p10[i]; e = e - (1 << i); }
  }
  if (w >= 10.0) e = e + 1;
  if (nsig > 18) nsig = 18;
  lim = 1;
  for (i = 0; i < nsig; i = i + 1) lim = lim * 10;
  n = scaleround(v, nsig - 1 - e);
  if (n >= lim) { e = e + 1; n = scaleround(v, nsig - 1 - e); }
  if (n < lim / 10) { e = e - 1; n = scaleround(v, nsig - 1 - e); }
  if (n >= lim) { n = n / 10; e = e + 1; }
  for (i = nsig - 1; i >= 0; i = i - 1) {
    k = (int)(n % 10);
    n = n / 10;
    digs[i] = (char)('0' + k);
  }
  digs[nsig] = 0;
  *ex = e;
  return 0;
}

static int putn(char *out, int at, int c) {
  out[at] = (char)c;
  return at + 1;
}

/* 指数の形 (d.ddde±XX) */
static int fmte(double v, int prec, int up, char *out) {
  char digs[24];
  int e;
  int i;
  int n;
  int ae;
  n = 0;
  if (v < 0) { out[n] = '-'; n = n + 1; v = -v; }
  if (v == 0.0) {
    e = 0;
    for (i = 0; i <= prec; i = i + 1) digs[i] = '0';
    digs[prec + 1] = 0;
  } else {
    digits(v, prec + 1, digs, &e);
  }
  n = putn(out, n, digs[0]);
  if (prec > 0) {
    n = putn(out, n, '.');
    for (i = 1; i <= prec; i = i + 1) n = putn(out, n, digs[i]);
  }
  n = putn(out, n, up ? 'E' : 'e');
  if (e < 0) { n = putn(out, n, '-'); ae = -e; } else { n = putn(out, n, '+'); ae = e; }
  if (ae >= 100) {
    n = putn(out, n, '0' + ae / 100);
    n = putn(out, n, '0' + (ae / 10) % 10);
    n = putn(out, n, '0' + ae % 10);
  } else {
    n = putn(out, n, '0' + ae / 10);
    n = putn(out, n, '0' + ae % 10);
  }
  out[n] = 0;
  return n;
}

/* 小数の形 (ddd.ddd)。
 *
 * 桁数が 18 に収まるなら**まるごと整数へ寄せて**組む —— こうすると
 * 丸めが scaleround の 1 か所だけになり，桁配列を並べ直す途中で
 * 半端の扱いが変わらない */
static int fmtf(double v, int prec, char *out) {
  char digs[24];
  char body[64];
  int e;
  int i;
  int n;
  int nsig;
  int ip;
  int bl;
  n = 0;
  if (v < 0) { out[n] = '-'; n = n + 1; v = -v; }
  if (v == 0.0) {
    n = putn(out, n, '0');
    if (prec > 0) {
      n = putn(out, n, '.');
      for (i = 0; i < prec; i = i + 1) n = putn(out, n, '0');
    }
    out[n] = 0;
    return n;
  }
  digits(v, 1, digs, &e);
  nsig = e + prec + 1;
  if (nsig <= 18) {
    unsigned long long u;
    u = scaleround(v, prec);
    bl = fmtint(u, 10, 0, body);
    if (bl <= prec) {
      /* 0.00ddd の形。整数部の 0 と足りない桁を補う */
      n = putn(out, n, '0');
      if (prec > 0) {
        n = putn(out, n, '.');
        for (i = 0; i < prec - bl; i = i + 1) n = putn(out, n, '0');
        for (i = 0; i < bl; i = i + 1) n = putn(out, n, body[i]);
      }
      out[n] = 0;
      return n;
    }
    for (i = 0; i < bl - prec; i = i + 1) n = putn(out, n, body[i]);
    if (prec > 0) {
      n = putn(out, n, '.');
      for (i = bl - prec; i < bl; i = i + 1) n = putn(out, n, body[i]);
    }
    out[n] = 0;
    return n;
  }
  /* 18 桁に収まらない。上位 18 桁だけを数え，残りは 0 で埋める ——
   * ホストは 2 進の値を正確に十進へ展開するので，ここから先は
   * 一致しない (5.8 に註がある) */
  nsig = 18;
  digits(v, nsig, digs, &e);
  ip = e + 1;
  if (ip <= 0) {
    n = putn(out, n, '0');
  } else {
    for (i = 0; i < ip; i = i + 1) {
      if (i < nsig) n = putn(out, n, digs[i]);
      else n = putn(out, n, '0');
    }
  }
  if (prec > 0) {
    n = putn(out, n, '.');
    for (i = 0; i < prec; i = i + 1) {
      int k;
      k = ip + i;
      if (k < 0 || k >= nsig) n = putn(out, n, '0');
      else n = putn(out, n, digs[k]);
    }
  }
  out[n] = 0;
  return n;
}

/* %g。指数が小さすぎるか大きすぎれば e の形，そうでなければ f の形。
 * 末尾の 0 を落とすのが %g の要点である */
static int fmtg(double v, int prec, int up, int alt, char *out) {
  char tmp[512];
  char digs[24];
  int e;
  int n;
  int i;
  int dot;
  if (prec == 0) prec = 1;
  if (v == 0.0) {
    e = 0;
  } else {
    digits(v < 0 ? -v : v, prec, digs, &e);
  }
  if (e < -4 || e >= prec) {
    n = fmte(v, prec - 1, up, tmp);
  } else {
    n = fmtf(v, prec - 1 - e, tmp);
  }
  if (!alt) {
    /* 末尾の 0 を落とす。指数部があれば小数部だけを見る */
    int epos;
    int last;
    epos = -1;
    for (i = 0; i < n; i = i + 1) {
      if (tmp[i] == 'e' || tmp[i] == 'E') { epos = i; break; }
    }
    dot = -1;
    for (i = 0; i < n; i = i + 1) {
      if (tmp[i] == '.') { dot = i; break; }
    }
    if (dot >= 0) {
      last = (epos < 0) ? n : epos;
      while (last > dot + 1 && tmp[last - 1] == '0') last = last - 1;
      if (last == dot + 1) last = dot;
      if (epos < 0) {
        tmp[last] = 0;
        n = last;
      } else {
        memmove(tmp + last, tmp + epos, (size_t)(n - epos + 1));
        n = last + (n - epos);
        tmp[n] = 0;
      }
    }
  }
  strcpy(out, tmp);
  return n;
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

int awk_fmtinit(void) {
  initp10();
  initpw10();
  return 0;
}

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

