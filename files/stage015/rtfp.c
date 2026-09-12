// 浮動小数点の実行時支援 (Stage 15 第 3 部)。
//
// **浮動小数点の型を使わずに書く。** double を 64 bit の bit の並びとして
// 受け取り，整数演算だけで組み立てる。したがってこのソース自体は第 2 部
// (64 bit 整数) までの cc でコンパイルでき，第 3 部の cc を待たずに
// 検査できる。
//
// 表現は IEEE 754 の binary64。
//
//   bit 63    符号
//   bit 62-52 指数 (下駄 1023)
//   bit 51-0  仮数の小数部 (正規化数は先頭に見えない 1 が付く)
//
// 丸めは最近接偶数 (round to nearest, ties to even) 固定。丸め方向を
// 変える手段は用意しない。
//
// float (binary32) の四則は **double を経由する**。double の仮数 53 bit は
// float の 24 bit の 2 倍 + 2 以上あるので，加減乗除では二重丸めが起きても
// 結果は変わらない (証明が知られている)。float 専用の実装を持たない分だけ
// 小さくできる。

// ---- 組み立てと取り出し ----

int dsign(unsigned long long a) { return (int)(a >> 63); }
int dexp(unsigned long long a) { return (int)((a >> 52) & 2047); }
unsigned long long dfrac(unsigned long long a) {
  return a & 0xfffffffffffffULL;
}

unsigned long long dpack(int s, int e, unsigned long long f) {
  unsigned long long r;
  r = ((unsigned long long)(unsigned)s) << 63;
  r = r | (((unsigned long long)(unsigned)e) << 52);
  return r | (f & 0xfffffffffffffULL);
}

int disnan(unsigned long long a) {
  return dexp(a) == 2047 && dfrac(a) != 0;
}
int disinf(unsigned long long a) {
  return dexp(a) == 2047 && dfrac(a) == 0;
}
int diszero(unsigned long long a) {
  return (a & 0x7fffffffffffffffULL) == 0;
}

unsigned long long dnan(void) { return 0x7ff8000000000000ULL; }
unsigned long long dinf(int s) { return dpack(s, 2047, 0ULL); }

// 先頭の 0 の個数 (x != 0 のとき 0..63)
int nlz64(unsigned long long x) {
  int n;
  n = 0;
  if ((x >> 32) == 0) { n = n + 32; x = x << 32; }
  if ((x >> 48) == 0) { n = n + 16; x = x << 16; }
  if ((x >> 56) == 0) { n = n + 8; x = x << 8; }
  if ((x >> 60) == 0) { n = n + 4; x = x << 4; }
  if ((x >> 62) == 0) { n = n + 2; x = x << 2; }
  if ((x >> 63) == 0) { n = n + 1; }
  return n;
}

// 仮数を取り出す。正規化数は見えない 1 を付け，非正規化数はそのまま。
// 返すのは 53 bit に収まる値で，*pe に「その仮数が 2^-52 単位で表す指数」を
// 入れる (つまり値は sig * 2^(*pe))。
unsigned long long dsig(unsigned long long a, int *pe) {
  int e;
  unsigned long long f;
  e = dexp(a);
  f = dfrac(a);
  if (e == 0) {
    *pe = -1074;              // 非正規化数
    return f;
  }
  *pe = e - 1075;             // 1023 の下駄 + 52 bit の小数部
  return f | 0x10000000000000ULL;
}

// 仮数 sig (0 でない) と指数 e から double を組む。sig は 2^e 単位。
// 55 bit 目までに丸めの手掛かり (guard/round/sticky) が入っていてよい。
// **ここが丸めの一箇所**である。
unsigned long long dnorm(int s, unsigned long long sig, int e) {
  int sh; int d; int E;
  unsigned long long rem;
  unsigned long long half;

  if (sig == 0) return dpack(s, 0, 0ULL);

  sh = 63 - nlz64(sig);              // 立っている最上位の bit 位置
  E = e + sh + 1075 - 52;            // 正規化数だとしたときの下駄付き指数

  if (E <= 0) {
    // 非正規化数。値 sig * 2^e を m * 2^-1074 で表す。
    // **丸めはここ 1 回だけ**にする。正規化してから落とすと二重に丸まり，
    // 最後の 1 bit がずれる
    d = 0 - 1074 - e;
    if (d <= 0) {
      if (0 - d >= 64) return dpack(s, 0, 0ULL);
      return dpack(s, 0, sig << (0 - d));
    }
    if (d > 64) return dpack(s, 0, 0ULL);
    if (d == 64) {
      // 最小の非正規化数の半分あたり。ちょうど半分は偶数へ (= 0)
      if (sig > (1ULL << 63)) return dpack(s, 0, 1ULL);
      return dpack(s, 0, 0ULL);
    }
    half = 1ULL << (d - 1);
    rem = sig & ((half << 1) - 1);
    sig = sig >> d;
    if (rem > half) sig = sig + 1;
    else if (rem == half) sig = sig + (sig & 1);
    if ((sig >> 52) != 0) return dpack(s, 1, 0ULL);   // 丸め上がりで最小の正規化数
    return dpack(s, 0, sig);
  }

  // 正規化数。仮数の最上位を bit 52 へ寄せる
  if (sh > 52) {
    d = sh - 52;
    half = 1ULL << (d - 1);
    rem = sig & ((half << 1) - 1);
    sig = sig >> d;
    e = e + d;
    if (rem > half) sig = sig + 1;
    else if (rem == half) sig = sig + (sig & 1);   // 偶数へ
    if ((sig >> 53) != 0) { sig = sig >> 1; e = e + 1; }  // 桁上がり
  } else if (sh < 52) {
    d = 52 - sh;
    sig = sig << d;
    e = e - d;
  }
  E = e + 1075;
  if (E >= 2047) return dinf(s);
  return dpack(s, E, sig & 0xfffffffffffffULL);
}

// ---- 加減算 ----

unsigned long long dadd2(unsigned long long a, unsigned long long b) {
  int sa; int sb; int ea; int eb; int e;
  unsigned long long ma; unsigned long long mb;
  unsigned long long t; int ti;
  int d;
  unsigned long long sticky;

  if (disnan(a) || disnan(b)) return dnan();
  if (disinf(a)) {
    if (disinf(b) && dsign(a) != dsign(b)) return dnan();
    return a;
  }
  if (disinf(b)) return b;
  if (diszero(a)) {
    if (diszero(b)) {
      // -0 + -0 だけが -0
      if (dsign(a) && dsign(b)) return a;
      return 0;
    }
    return b;
  }
  if (diszero(b)) return a;

  sa = dsign(a); sb = dsign(b);
  ma = dsig(a, &ea);
  mb = dsig(b, &eb);

  // 桁を揃える。落ちる分は sticky に残す (丸めのため)
  if (ea < eb) {
    t = ma; ma = mb; mb = t;
    ti = ea; ea = eb; eb = ti;
    ti = sa; sa = sb; sb = ti;
  }
  d = ea - eb;
  // 3 bit ぶん余裕を持たせてから寄せる
  ma = ma << 3;
  mb = mb << 3;
  sticky = 0;
  if (d > 60) {
    sticky = mb;
    mb = 0;
  } else if (d > 0) {
    sticky = mb & ((1ULL << d) - 1);
    mb = mb >> d;
  }
  if (sticky != 0) mb = mb | 1;
  e = ea - 3;

  if (sa == sb) return dnorm(sa, ma + mb, e);
  if (ma >= mb) {
    if (ma == mb) return 0;       // 打ち消し合いは +0
    return dnorm(sa, ma - mb, e);
  }
  return dnorm(sb, mb - ma, e);
}

unsigned long long __dadd(unsigned long long a, unsigned long long b) {
  return dadd2(a, b);
}

unsigned long long __dneg(unsigned long long a) {
  return a ^ 0x8000000000000000ULL;
}

unsigned long long __dsub(unsigned long long a, unsigned long long b) {
  return dadd2(a, __dneg(b));
}

// ---- 乗算 ----

// 64x64 -> 128。上位を *hi に入れ，下位を返す
unsigned long long mul128(unsigned long long a, unsigned long long b,
                          unsigned long long *hi) {
  unsigned long long a0; unsigned long long a1;
  unsigned long long b0; unsigned long long b1;
  unsigned long long p00; unsigned long long p01;
  unsigned long long p10; unsigned long long p11;
  unsigned long long mid;

  a0 = a & 0xffffffffULL; a1 = a >> 32;
  b0 = b & 0xffffffffULL; b1 = b >> 32;
  p00 = a0 * b0; p01 = a0 * b1; p10 = a1 * b0; p11 = a1 * b1;
  mid = (p00 >> 32) + (p01 & 0xffffffffULL) + (p10 & 0xffffffffULL);
  *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return (mid << 32) | (p00 & 0xffffffffULL);
}

unsigned long long __dmul(unsigned long long a, unsigned long long b) {
  int sa; int sb; int s; int ea; int eb;
  unsigned long long ma; unsigned long long mb;
  unsigned long long lo; unsigned long long hi; unsigned long long st;
  int drop;

  if (disnan(a) || disnan(b)) return dnan();
  sa = dsign(a); sb = dsign(b); s = sa ^ sb;
  if (disinf(a)) {
    if (diszero(b)) return dnan();
    return dinf(s);
  }
  if (disinf(b)) {
    if (diszero(a)) return dnan();
    return dinf(s);
  }
  if (diszero(a) || diszero(b)) return dpack(s, 0, 0ULL);

  ma = dsig(a, &ea);
  mb = dsig(b, &eb);
  lo = mul128(ma, mb, &hi);

  // 積は最大 106 bit。64 bit へ落とし，落とした分は sticky 1 bit にまとめる。
  // ma と mb は 53 bit 以下なので hi は 42 bit 以下に収まり，
  // 下の桁送りが 64 に達することはない
  if (hi != 0) {
    drop = 64 - nlz64(hi);
    st = lo & ((1ULL << drop) - 1);
    hi = (hi << (64 - drop)) | (lo >> drop);
    if (st != 0) hi = hi | 1;
    return dnorm(s, hi, ea + eb + drop);
  }
  return dnorm(s, lo, ea + eb);
}

// ---- 除算 ----

unsigned long long __ddiv(unsigned long long a, unsigned long long b) {
  int sa; int sb; int s; int ea; int eb; int i; int sh;
  unsigned long long ma; unsigned long long mb;
  unsigned long long q; unsigned long long r;

  if (disnan(a) || disnan(b)) return dnan();
  sa = dsign(a); sb = dsign(b); s = sa ^ sb;
  if (disinf(a)) {
    if (disinf(b)) return dnan();
    return dinf(s);
  }
  if (disinf(b)) return dpack(s, 0, 0ULL);
  if (diszero(b)) {
    if (diszero(a)) return dnan();
    return dinf(s);
  }
  if (diszero(a)) return dpack(s, 0, 0ULL);

  ma = dsig(a, &ea);
  mb = dsig(b, &eb);

  // **両方を 53 bit へ揃えてから割る。** 非正規化数はそのままだと
  // 桁数が足りず，下の繰返しで余りが溢れる
  sh = 52 - (63 - nlz64(ma));
  if (sh > 0) { ma = ma << sh; ea = ea - sh; }
  sh = 52 - (63 - nlz64(mb));
  if (sh > 0) { mb = mb << sh; eb = eb - sh; }

  // 引き算しながら 56 桁立てる。q は ma/mb を 2^55 倍した値になる
  // (最初の桁は送る前に立てるので 2^56 ではない)
  q = 0; r = ma;
  i = 0;
  while (i < 56) {
    q = q << 1;
    if (r >= mb) { r = r - mb; q = q | 1; }
    r = r << 1;
    i = i + 1;
  }
  if (r != 0) q = q | 1;          // 割り切れなかった印 (sticky)
  return dnorm(s, q, ea - eb - 55);
}

// ---- 比較 ----
// 返り値: 0 = 等しい, 1 = a > b, -1 = a < b, 2 = 比較できない (NaN)

int __dcmp(unsigned long long a, unsigned long long b) {
  int sa; int sb;
  if (disnan(a) || disnan(b)) return 2;
  if (diszero(a) && diszero(b)) return 0;      // +0 == -0
  sa = dsign(a); sb = dsign(b);
  if (sa != sb) { if (sa) return -1; return 1; }
  if (a == b) return 0;
  // 同じ符号なら bit の並びの大小がそのまま値の大小になる
  if ((a & 0x7fffffffffffffffULL) > (b & 0x7fffffffffffffffULL)) {
    if (sa) return -1;
    return 1;
  }
  if (sa) return 1;
  return -1;
}

// ---- 整数との行き来 ----

unsigned long long __ll2d(long long v) {
  int s;
  unsigned long long u;
  if (v == 0) return 0;
  s = 0;
  if (v < 0) { s = 1; u = (unsigned long long)(0 - v); }
  else u = (unsigned long long)v;
  return dnorm(s, u, 0);
}

unsigned long long __ull2d(unsigned long long v) {
  if (v == 0) return 0;
  return dnorm(0, v, 0);
}

unsigned long long __i2d(int v) { return __ll2d((long long)v); }
unsigned long long __u2d(unsigned int v) {
  return __ull2d((unsigned long long)v);
}

// 0 方向への切捨て。範囲外と NaN の値は決めておく (C は未定義)
long long __d2ll(unsigned long long a) {
  int s; int e;
  unsigned long long m;
  if (disnan(a)) return 0;
  s = dsign(a);
  m = dsig(a, &e);
  if (m == 0) return 0;
  if (e > 0) {
    if (e >= 64) { if (s) return (long long)0x8000000000000000ULL;
                   return (long long)0x7fffffffffffffffULL; }
    m = m << e;
  } else if (e < 0) {
    if (0 - e >= 64) return 0;
    m = m >> (0 - e);
  }
  if (s) return 0 - (long long)m;
  return (long long)m;
}

unsigned long long __d2ull(unsigned long long a) {
  int e;
  unsigned long long m;
  if (disnan(a) || dsign(a)) return 0;
  m = dsig(a, &e);
  if (m == 0) return 0;
  if (e > 0) {
    if (e >= 64) return 0xffffffffffffffffULL;
    m = m << e;
  } else if (e < 0) {
    if (0 - e >= 64) return 0;
    m = m >> (0 - e);
  }
  return m;
}

int __d2i(unsigned long long a) { return (int)__d2ll(a); }
unsigned int __d2u(unsigned long long a) {
  return (unsigned int)__d2ull(a);
}

// ---- float (binary32) との行き来 ----

unsigned long long __f2d(unsigned int f) {
  int s; int e;
  unsigned long long m;
  s = (int)(f >> 31);
  e = (int)((f >> 23) & 255);
  m = (unsigned long long)(f & 0x7fffff);
  if (e == 255) {
    if (m == 0) return dinf(s);
    return dnan();
  }
  if (e == 0) {
    if (m == 0) return dpack(s, 0, 0ULL);
    return dnorm(s, m, -149);          // 非正規化数
  }
  return dnorm(s, m | 0x800000ULL, e - 150);
}

unsigned int __d2f(unsigned long long a) {
  int s; int e; int sh; int d; int E;
  unsigned long long m;
  unsigned long long rem; unsigned long long half;

  s = dsign(a);
  if (disnan(a)) return 0x7fc00000;
  if (disinf(a)) return ((unsigned)s << 31) | 0x7f800000;
  m = dsig(a, &e);
  if (m == 0) return (unsigned)s << 31;

  sh = 63 - nlz64(m);
  E = e + sh - 23 + 150;
  if (E <= 0) {
    // 非正規化数。dnorm と同じく丸めは 1 回だけ
    d = 0 - 149 - e;
    if (d <= 0) {
      if (0 - d >= 32) return (unsigned)s << 31;
      return ((unsigned)s << 31) | (unsigned)(m << (0 - d));
    }
    if (d > 64) return (unsigned)s << 31;
    if (d == 64) {
      if (m > (1ULL << 63)) return ((unsigned)s << 31) | 1;
      return (unsigned)s << 31;
    }
    half = 1ULL << (d - 1);
    rem = m & ((half << 1) - 1);
    m = m >> d;
    if (rem > half) m = m + 1;
    else if (rem == half) m = m + (m & 1);
    if ((m >> 23) != 0) return ((unsigned)s << 31) | 0x00800000;
    return ((unsigned)s << 31) | (unsigned)m;
  }

  if (sh > 23) {
    d = sh - 23;
    half = 1ULL << (d - 1);
    rem = m & ((half << 1) - 1);
    m = m >> d;
    e = e + d;
    if (rem > half) m = m + 1;
    else if (rem == half) m = m + (m & 1);
    if ((m >> 24) != 0) { m = m >> 1; e = e + 1; }
  } else if (sh < 23) {
    d = 23 - sh;
    m = m << d;
    e = e - d;
  }
  E = e + 150;
  if (E >= 255) return ((unsigned)s << 31) | 0x7f800000;
  return ((unsigned)s << 31) | ((unsigned)E << 23)
         | ((unsigned)m & 0x7fffff);
}

// float の四則は double を経由する (冒頭の注記)
unsigned int __fadd(unsigned int a, unsigned int b) {
  return __d2f(__dadd(__f2d(a), __f2d(b)));
}
unsigned int __fsub(unsigned int a, unsigned int b) {
  return __d2f(__dsub(__f2d(a), __f2d(b)));
}
unsigned int __fmul(unsigned int a, unsigned int b) {
  return __d2f(__dmul(__f2d(a), __f2d(b)));
}
unsigned int __fdiv(unsigned int a, unsigned int b) {
  return __d2f(__ddiv(__f2d(a), __f2d(b)));
}
int __fcmp(unsigned int a, unsigned int b) {
  return __dcmp(__f2d(a), __f2d(b));
}
