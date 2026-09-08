/* GNU の文式 ({ 文...; 式; })。cc15y。docs/stage018-ext.md 7。
 *
 * GCC とカーネルのヘッダは，これをマクロの中で使う —— 引数を 1 度だけ
 * 評価する max / min がその代表である。マクロなので**呼ぶ側のソースを
 * 直しても避けられない**。
 *
 * 期待出力: abcdefg */
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }

#define MAX(a, b) ({ int _a = (a); int _b = (b); _a > _b ? _a : _b; })

int side;
int bump(int v) { side = side + 1; return v; }

int main() {
  int x;
  int i;

  chk('a', ({ 1; }) == 1);
  chk('b', ({ int t; t = 2; t + 3; }) == 5);

  /* 文を含む。値は最後の式文のもの */
  chk('c', ({ int t; t = 0; for (i = 0; i < 4; i = i + 1) t = t + i; t; }) == 6);

  /* マクロの中で使う形。**引数は 1 度しか評価されない** */
  side = 0;
  x = MAX(bump(3), bump(7));
  chk('d', x == 7);
  chk('e', side == 2);

  /* 入れ子 */
  chk('f', ({ int t; t = ({ 4; }) + ({ 5; }); t; }) == 9);

  /* 名前は文式を抜けると消える (同じ名前を外側で使い直せる) */
  x = 11;
  chk('g', ({ int x; x = 1; x; }) == 1 && x == 11);
  putc(10);
  return 0;
}
