/* ctype.h の分類表を，ホストの処理系と 1 文字ずつ突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.3)。
 *
 * 我々の ctype は "C" ロケール固定なので，ホストの既定ロケール
 * ("C") と同じ表になるはずである。**はずである，を測る。**
 *
 * 0x00〜0x7f の 128 文字ぶんを 1 関数 1 行の '0' / '1' の並びで出す。
 * 食い違えば diff がその行を指し，何文字目かは列で判る。
 *
 * 0x80 以上は渡さない —— C89 は unsigned char の値か EOF 以外を
 * 未定義とし，我々もホストも定義していない。 */
#include <stdio.h>
#include <ctype.h>

static void row(char *tag, int (*f)(int)) {
  int c;
  printf("%s ", tag);
  for (c = 0; c < 128; c = c + 1) printf("%c", f(c) ? '1' : '0');
  printf("\n");
}

int main(void) {
  int c;

  row("isalnum", isalnum);
  row("isalpha", isalpha);
  row("iscntrl", iscntrl);
  row("isdigit", isdigit);
  row("isgraph", isgraph);
  row("islower", islower);
  row("isprint", isprint);
  row("ispunct", ispunct);
  row("isspace", isspace);
  row("isupper", isupper);
  row("isxdigt", isxdigit);

  /* 変換は「変わった文字だけ」を並べる。変わらない文字はそのまま
   * 返るのが C89 の定めで，そこを取り違えると必ずこの行に出る */
  printf("toupper ");
  for (c = 0; c < 128; c = c + 1)
    if (toupper(c) != c) printf("%d>%d ", c, toupper(c));
  printf("\n");
  printf("tolower ");
  for (c = 0; c < 128; c = c + 1)
    if (tolower(c) != c) printf("%d>%d ", c, tolower(c));
  printf("\n");

  /* EOF を渡しても落ちないこと (実装の範囲比較が下限も見ているか) */
  printf("eof %d %d %d %d\n",
         isalpha(EOF) ? 1 : 0, isdigit(EOF) ? 1 : 0,
         toupper(EOF), tolower(EOF));
  return 0;
}
