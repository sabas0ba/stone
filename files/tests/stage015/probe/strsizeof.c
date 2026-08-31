/* **文字列リテラルの型は char[n+1] である** (C89 6.1.4)。
 *
 * cc15q までは estr2() が earr / esz / eaty を立てなかったので，
 * sizeof が常に「ポインタの大きさ」を返していた。**通ってしまうが値が
 * 違う** —— 台帳で bad と呼ぶ状態である。cc15r で直した
 * (docs/stage017-cc.md 31 章)。
 *
 * 表に出た形は tcc が書庫 (.a) を読めないことだった。tccelf.c は
 *
 *     #define ARMAG "!<arch>\n"
 *     file_offset = sizeof ARMAG - 1;
 *
 * で最初の見出しの位置を求める。8 のはずが 3 になり，見出しでない
 * 場所を読んで "invalid archive" と言う。
 *
 * 25 章・28 章で直した「静的な初期化子の中の文字列リテラル」と同じ根
 * である。あちらは初期化子の側，こちらは式の側 (sizeof と単項 &)。
 * **退化を壊していないこと**も一緒に見る —— 式の中では今までどおり
 * 先頭要素へのポインタでなければならない。 */
int putc(int c);

int f(void) {
  char *p;

  /* sizeof は退化前 (char[n+1]) を見る */
  if (sizeof "" != 1) return 'n';
  if (sizeof "a" != 2) return 'n';
  if (sizeof "abc" != 4) return 'n';
  if (sizeof("!<arch>\n") != 9) return 'n';
  /* tccelf.c 3681 行そのもの */
  if (sizeof "!<arch>\n" - 1 != 8) return 'n';
  /* 4 の倍数でちょうど終わる形 (文字列プールの詰め方に依らないこと) */
  if (sizeof "abcdefg" != 8) return 'n';
  if (sizeof "abcdefgh" != 9) return 'n';
  /* 逃げの並びも 1 文字である */
  if (sizeof "\n\t\\\"" != 5) return 'n';

  /* **式の中では退化する。** ここを壊すと今まで通っていたものが全部
   * 壊れるので，一緒に見る */
  p = "abc";
  if (p[0] != 'a' || p[3] != 0) return 'n';
  if (sizeof p != 4) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
