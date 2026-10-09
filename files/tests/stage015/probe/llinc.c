/* 64 bit の ++ / -- を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.15 / stage015/cc15aq.sc。
 *
 * cc15ap までは下位語だけを読み書きしていたので，下位語が 0xffffffff と
 * 0 の間を回るときに上位語へ桁上がり (桁借り) しなかった。式の値の
 * 上位語も前の式の残りだった。GCC の ivopts の
 * `for (i = -MAX_RATIO; i <= MAX_RATIO; i++)` (i は HOST_WIDE_INT) が
 * -1 から 0 へ進めず，cc1 が -O2 で記憶域を使い果たした。
 *
 * 見るもの: 前置・後置の ++ / -- が語の境を越えること，式の値 (上位語を
 * 含む)，符号なし，ポインタの先の 64 bit (*p++ の左辺の評価が 1 回)，
 * ivopts と同じ形のループの回数。 */
int putc(int c);

static void pn(long long v) {
  char b[24];
  int n;
  n = 0;
  if (v < 0) { putc('-'); v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}

#define MAX_RATIO 128

int main(void) {
  long long i;
  long long a[3];
  long long *p;
  unsigned long long u;
  int n;
  n = 0;
  for (i = -MAX_RATIO; i <= MAX_RATIO; i++) {
    n = n + 1;
    if (n > 1000) break;
  }
  pn(n); pn(i);
  i = -1; pn(i++); pn(i);
  pn(++i); pn(i--); pn(--i); pn(i);
  i = 4294967295LL; pn(++i); pn(i);
  i = 4294967296LL; pn(i--); pn(i);
  u = 4294967295ULL; u++; pn((long long)(u >> 32)); pn((long long)(u & 4294967295ULL));
  u = 0; u--; pn((long long)(u >> 32));
  a[0] = -1; a[1] = 4294967295LL; a[2] = 7;
  p = a;
  (*p++)++;
  ++*p;
  pn(a[0]); pn(a[1]); pn(a[2]); pn((long long)(p - a));
  putc('\n');
  return 0;
}
