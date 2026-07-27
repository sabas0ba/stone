// 反復と分岐の組合せ。goto によるループ脱出とラベルの前方参照も含む。
char ob[16];
int pn(int v) {
  int n;
  if (v < 0) { putc('-'); v = -v; }
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n = n + 1; v = v / 10; }
  while (n > 0) { n = n - 1; putc(ob[n]); }
  putc(' ');
  return 0;
}
int prime(int n) {
  int d;
  if (n < 2) return 0;
  for (d = 2; d * d <= n; d++)
    if (n % d == 0) return 0;
  return 1;
}
int main() {
  int i; int j; int n; int s;
  // 素数を数える (for + 早期 return)
  n = 0;
  for (i = 0; i < 50; i++) if (prime(i)) n++;
  pn(n);                            // 15
  // 二重ループを goto で一気に抜ける
  s = 0;
  for (i = 0; i < 10; i++) {
    for (j = 0; j < 10; j++) {
      if (i * j > 20) goto out;
      s += i * j;
    }
  }
out:
  pn(s);                            // 198 (i=0..2 で 135, i=3 は j=7 で脱出)
  // do-while + switch でパリティを数える
  i = 0; s = 0;
  do {
    switch (i % 3) {
    case 0:
    case 1: s++; break;
    default: s += 10;
    }
    i++;
  } while (i < 9);
  pn(s);                            // 6 + 30 = 36
  putc(10);
  return 0;
}
