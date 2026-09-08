/* C89 の外側の第 1 陣 (cc15w。docs/stage018-ext.md)。
 *
 *   - 文の後ろに置いた宣言 (C99 6.8.2)
 *   - for の初期化での宣言と，その名前の見える範囲 (C99 6.8.5.3)
 *   - GNU の別綴り (__extension__ / __inline__ / __const__ /
 *     __volatile__ / __signed__ / __restrict__ / restrict)
 *
 * どれも**実物のソースが当たり前に使う**書き方である。我々のソースは
 * 1 つも使っていないので，自分自身を組む限り永久に表に出ない。
 *
 * 期待出力: abcdefgh */
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }

/* 別綴りは宣言のどこに来てもよい */
static __inline__ int twice(__const__ int v) { return v + v; }
__extension__ static int thrice(int v) { return v + v + v; }

int sum(__const__ int *__restrict__ a, int n) {
  int t;
  t = 0;
  for (int i = 0; i < n; i = i + 1) t = t + a[i];
  return t;
}

int main() {
  int a;
  a = 1;
  chk('a', a == 1);

  /* 文の後ろの宣言。ここから下では b が見える */
  int b;
  b = twice(a);
  chk('b', b == 2);
  chk('c', thrice(b) == 6);

  /* for の初期化での宣言。i は for の中だけ */
  for (int i = 0; i < 3; i = i + 1) b = b + i;
  chk('d', b == 5);

  /* 同じ名前を外側で使い直せる (前の i は見えなくなっている) */
  int i;
  i = 100;
  for (int i = 0; i < 2; i = i + 1) b = b + 10;
  chk('e', b == 25);
  chk('f', i == 100);

  __extension__ int c = 7;
  chk('g', c == 7);

  {
    int v[4];
    v[0] = 1; v[1] = 2; v[2] = 3; v[3] = 4;
    chk('h', sum(v, 4) == 10);
  }
  putc(10);
  return 0;
}
