/* 関数内 static の初期化子が**文字列リテラルへのポインタ**のとき。
 *
 * tcc の tccgen.c の parse_atomic() がこの形を使う。
 *
 *   static const char *const templates[] = { "alm.?", "Asm.v", ... };
 *
 * ここが壊れると，引数の個数を駆動する文字が読めず，正しい C を
 * 誤りとして拒む (docs/stage017-cc.md 24 章)。
 *
 * **通ってしまうが値が違う**ので bad である。整数の初期化子
 * (staticinit) も，文字の配列を文字列で埋める形 (バイトを写すので
 * ポインタが要らない) も，ファイル有効域の同じ形も正しい。
 * 壊れるのは「関数内 static + 文字列へのポインタ」だけである。
 */
int putc(int c);
int f(void) {
  static char *p = "A";
  static char *t[2] = { "B", "C" };
  if (p[0] != 'A') return 'n';
  if (t[0][0] != 'B') return 'n';
  if (t[1][0] != 'C') return 'n';
  return 'y';
}
int main(void) { putc(f()); putc('\n'); return 0; }
