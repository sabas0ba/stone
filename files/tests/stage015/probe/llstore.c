/* 64 bit の記憶と代入。局所・大域・構造体メンバを往復させ，
 * 上位語と下位語の両方が正しいことと，隣のメンバを壊さないことを見る */
int putc(int c);
long long g;
struct s { int a; long long v; int b; };
struct s st;
int lo(long long *p) { int *q; q = (int *)p; return q[0]; }
int hi(long long *p) { int *q; q = (int *)p; return q[1]; }
int hx(int v) {
  int i; int d;
  for (i = 7; i >= 0; i = i - 1) {
    d = (v >> (i * 4)) & 15;
    if (d < 10) putc('0' + d); else putc('a' + d - 10);
  }
  return 0;
}
int main(void) {
  long long x;
  long long y;
  x = 81985529216486895LL;
  y = x;
  g = y;
  st.a = 1; st.b = 2;
  st.v = g;
  hx(hi(&g)); hx(lo(&g)); putc(':');
  hx(hi(&st.v)); hx(lo(&st.v)); putc(':');
  hx(st.a); hx(st.b);
  putc('\n');
  return 0;
}
