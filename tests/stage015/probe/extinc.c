/* 不完全型の extern 宣言を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.11 / stage015/cc15ag.sc。
 *
 * C89 は，記憶域を取らない extern の宣言を不完全型のまま許す。GCC の ggc.h
 * は `extern struct alloc_zone rtl_zone;` と書き，struct alloc_zone を
 * どこにも定義しない。cc15af までは大きさが判らないとして 5 で拒んでいた。
 *
 * 後で型を完全にして同じ名前を定義したとき，**その大きさで記憶域を取ること**
 * を値で見る (大きさを 0 のまま取ると，隣の変数と重なる)。 */
int putc(int c);

static void pn(int v) {
  char b[16];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void nl(void) { putc('\n'); }

/* 定義の無い不完全型の extern (GCC の ggc.h の rtl_zone と同じ形) は
 * ここに置かない。**我々の cc は使わない extern も未定義の記号として .o に
 * 出し，我々の ld はそれを解けずに落ちる** (ホストの処理系は参照の無い
 * extern を記号にしない)。翻訳は通るので cc の適合の話ではなく，繋ぐ段の
 * 話である (docs/stage017-gcc.md 8.11)。cc1 を繋ぐ段で扱う */

/* 後で完全になり，本定義される形 */
extern struct pair p;
struct pair { int a; int b; int c; };
struct pair p;
int guard;

int main(void) {
  p.a = 1;
  p.b = 2;
  p.c = 3;
  guard = 99;
  pn(p.a + p.b + p.c);
  pn(guard);
  pn((int)sizeof p);
  nl();
  return 0;
}
