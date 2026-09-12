/* rt64.c --- 64 bit の除算・剰余 (実行時支援)
 *
 * RV32 に 64 bit の除算命令は無く，シフトと引き算で組む必要がある。
 * 展開すると呼出し 1 つあたりの命令が多くなるので，**関数にして呼ぶ**
 * (docs/stage015-tcc.md 6.2 の項目 9)。cc は long long の / と % を
 * ここへの呼出しへ落とす。
 *
 * この 4 つは cc 自身が名前で知っている。名前を libgcc 風の __divdi3 に
 * しないのは，引数の渡し方が本処理系の規約 (データスタック経由) であり，
 * 同じ名前で別物を指すと紛らわしいためである。
 *
 * 本ファイルは 64 bit の / と % を使わずに書いてある。使うと自分自身を
 * 呼ぶことになる。
 */

typedef unsigned long long u64;
typedef long long i64;

/* 引き算とシフトで割る。r に上位から 1 bit ずつ下ろし，b を引けるなら
 * 引いて商のその桁を立てる (筆算と同じ) */
static u64 udivmod(u64 a, u64 b, int wantrem) {
  u64 q;
  u64 r;
  int i;
  q = 0;
  r = 0;
  /* 0 除算は 0 を返す。RV32 の div が -1 を返すのと同じく「例外を出さず
   * 値を決める」立場を取る (docs/stage005-sc.md 2.8 と同じ考え方) */
  if (b == 0) return 0;
  i = 63;
  while (i >= 0) {
    r = r << 1;
    r = r | ((a >> i) & 1LL);
    if (r >= b) {
      r = r - b;
      q = q | (1LL << i);
    }
    i = i - 1;
  }
  if (wantrem) return r;
  return q;
}

u64 __udiv64(u64 a, u64 b) { return udivmod(a, b, 0); }
u64 __umod64(u64 a, u64 b) { return udivmod(a, b, 1); }

i64 __div64(i64 a, i64 b) {
  int neg;
  u64 q;
  neg = 0;
  if (a < 0) { a = 0 - a; neg = neg ^ 1; }
  if (b < 0) { b = 0 - b; neg = neg ^ 1; }
  q = udivmod(a, b, 0);
  if (neg) return 0 - (i64)q;
  return (i64)q;
}

/* 剰余の符号は被除数に合わせる (C89 の処理系定義を そう定める) */
i64 __mod64(i64 a, i64 b) {
  int neg;
  u64 r;
  neg = 0;
  if (a < 0) { a = 0 - a; neg = 1; }
  if (b < 0) b = 0 - b;
  r = udivmod(a, b, 1);
  if (neg) return 0 - (i64)r;
  return (i64)r;
}
