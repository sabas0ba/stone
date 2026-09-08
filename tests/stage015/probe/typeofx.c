/* typeof / __typeof__ (GNU C。cc15z。docs/stage018-ext.md 9)。
 *
 * カーネルのヘッダは container_of / min / max をこの形で書く。
 * 文式と組で使われることが多い —— 引数の型を持ち回るためである。
 *
 * 期待出力: abcdefg */
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }

#define MIN(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); \
                     _a < _b ? _a : _b; })

static int gi;
static char gc;
static int ga[4];

int main() {
  typeof(gi) a;
  __typeof__(gc) c;
  typeof(&gi) p;
  int i;

  a = 7;
  chk('a', a == 7);
  c = 'z';
  chk('b', c == 'z');
  p = &gi;
  gi = 3;
  chk('c', *p == 3);

  /* 式の型。char は 1 バイトのまま */
  chk('d', sizeof(typeof(gc)) == 1 && sizeof(typeof(gi)) == 4);
  /* 配列は退化させない */
  chk('e', sizeof(typeof(ga)) == 16);
  /* 型そのものも書ける */
  chk('f', sizeof(typeof(long long)) == 8);

  /* 文式と組で使う形 (カーネルの min) */
  i = MIN(4, 9);
  chk('g', i == 4);
  putc(10);
  return 0;
}
