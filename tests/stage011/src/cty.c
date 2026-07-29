/* ctype.h の値の照合 (docs/stage011-libc.md 3.3, 5 章)
 *
 * 各分類の境界 (両端とその外側 1 文字) と，EOF (-1) を渡しても
 * 偽を返す (落ちない) ことを見る。
 *
 * リンクするのは ctype.o だけである (リンクの単位の検査を兼ねる)。
 */
#include <ctype.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

int main() {
  int eof;
  eof = -1;

  /* isdigit: '0'..'9'。'/' と ':' は両隣 */
  ok(isdigit('0') && isdigit('9'));
  ok(!isdigit('/') && !isdigit(':'));

  /* isupper / islower: '@' '[' '`' '{' は両隣 */
  ok(isupper('A') && isupper('Z'));
  ok(!isupper('@') && !isupper('[') && !isupper('a'));
  ok(islower('a') && islower('z'));
  ok(!islower('`') && !islower('{') && !islower('A'));

  /* isalpha / isalnum */
  ok(isalpha('A') && isalpha('z'));
  ok(!isalpha('0') && !isalpha(' '));
  ok(isalnum('A') && isalnum('z') && isalnum('0') && isalnum('9'));
  ok(!isalnum('_') && !isalnum(' '));

  /* isxdigit */
  ok(isxdigit('0') && isxdigit('9') && isxdigit('a') && isxdigit('f'));
  ok(isxdigit('A') && isxdigit('F'));
  ok(!isxdigit('g') && !isxdigit('G'));

  /* isspace: 6 種のみ */
  ok(isspace(' ') && isspace('\t') && isspace('\n'));
  ok(isspace('\v') && isspace('\f') && isspace('\r'));
  ok(!isspace('a') && !isspace(0));

  /* iscntrl: 0..31 と 127 */
  ok(iscntrl(0) && iscntrl(31) && iscntrl(127));
  ok(!iscntrl(32) && !iscntrl(126));

  /* isprint: 32..126。isgraph は空白を含まない */
  ok(isprint(' ') && isprint('~'));
  ok(!isprint(31) && !isprint(127));
  ok(isgraph('!') && isgraph('~'));
  ok(!isgraph(' ') && !isgraph(127));

  /* ispunct: 印字できて英数字でも空白でもない */
  ok(ispunct('!') && ispunct('@') && ispunct('~'));
  ok(!ispunct('a') && !ispunct('0') && !ispunct(' '));

  /* tolower / toupper: 対象外の文字は素通し */
  ok(tolower('A') == 'a' && tolower('Z') == 'z');
  ok(tolower('a') == 'a' && tolower('0') == '0');
  ok(toupper('a') == 'A' && toupper('z') == 'Z');
  ok(toupper('A') == 'A' && toupper('0') == '0');

  /* EOF (-1) はどの分類にも入らず，変換は素通し */
  ok(!isdigit(eof) && !isupper(eof) && !islower(eof));
  ok(!isalpha(eof) && !isalnum(eof) && !isxdigit(eof));
  ok(!isspace(eof) && !iscntrl(eof));
  ok(!isprint(eof) && !isgraph(eof) && !ispunct(eof));
  ok(tolower(eof) == eof && toupper(eof) == eof);

  putc('\n');
  return 0;
}
