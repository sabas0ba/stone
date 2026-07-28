// 第 3 部の 4: 構造体の返却 (docs/stage010-c89.md 18 章)
//
// 返却値の受渡しは 1 語なので，複数語を返すには呼んだ側が呼出しの直後に
// 引き取る必要がある。見たいのは値そのものより **引取りとスタックの後始末**
// なので，
//   - 返却値をそのまま次の呼出しへ渡す (入れ子)
//   - 返却値のメンバだけを取る (一時領域の扱い)
//   - 1 語に収まらない大きさ (6 語) を返す
//   - 繰り返し呼んでもデータスタックが減らないこと
// を確かめる。
struct P { int x; int y; int z; };
struct Big { int a[6]; };
char ob[16];
int pn(int v) {
  int n;
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n = n + 1; v = v / 10; }
  while (n > 0) { n = n - 1; putc(ob[n]); }
  putc(' ');
  return 0;
}
int enc(struct P p) { return p.x * 100 + p.y * 10 + p.z; }
struct P mk(int x, int y, int z) {
  struct P p;
  p.x = x; p.y = y; p.z = z;
  return p;
}
struct P addp(struct P a, struct P b) {
  return mk(a.x + b.x, a.y + b.y, a.z + b.z);
}
struct Big mkbig(int k) {
  struct Big b;
  int i;
  for (i = 0; i < 6; i++) b.a[i] = k + i;
  return b;
}
struct P gp;
int main() {
  struct P p;
  struct Big g;
  int i;
  p = mk(1, 2, 3);
  pn(enc(p));
  pn(enc(mk(4, 5, 6)));
  pn(enc(addp(mk(1, 2, 3), mk(4, 5, 6))));
  gp = mk(7, 8, 9);
  pn(enc(gp));
  pn(mk(1, 2, 3).y);
  g = mkbig(10);
  for (i = 0; i < 6; i++) pn(g.a[i]);
  for (i = 0; i < 3; i++) pn(enc(mk(i, i, i)));
  // 繰り返してもデータスタックが減らないこと (引取り後の後始末の検査)
  for (i = 0; i < 200; i++) mk(1, 2, 3);
  pn(enc(mk(9, 9, 9)));
  putc(10);
  return 0;
}
