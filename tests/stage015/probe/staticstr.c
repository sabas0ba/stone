/* 関数内 static の初期化子が**文字列リテラルへのポインタ**のとき。
 *
 * cc15p まではここが壊れていた。予約したローカルシンボルの位置
 * (lsoff) を誰も埋めないので，ポインタが別の場所を指し，**通って
 * しまうが値が違う**状態になっていた (docs/stage017-cc.md 24〜25 章)。
 * cc15q で直した。
 *
 * tcc の tccgen.c の parse_atomic() が
 *   static const char *const templates[] = { "alm.?", "Asm.v", ... };
 * という形で使う。ここが壊れると引数の個数を駆動する文字が読めず，
 * 正しい C を誤りとして拒む。
 *
 * Stage 14 の台帳には cc14g で測った bad が載っている。こちらは
 * 最前線の世代で測るので ok である。 */
int putc(int c);
int f(void) {
  static char *p = "A";
  static char *t[2] = { "B", "C" };
  if (p[0] != 'A') return 'n';
  if (t[0][0] != 'B') return 'n';
  if (t[1][0] != 'C') return 'n';
  return 'y';
}
int main() { putc(f()); putc('\n'); return 0; }
