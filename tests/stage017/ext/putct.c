/* **stone の OS の上で 2 引数の putc を使う** (docs/stage017-gcc.md 8.3 の 8)。
 *
 * libc22 は putc をマクロとして置いた (C89 7.9.1 / 7.9.7.8)。
 * header を足しただけでは「訳せた」までしか言えないので、
 * **fputc へ書き換わった先が本当に書けること**をここで見る。
 *
 * stdout と stderr の両方へ書く。GCC の libcpp は
 * cpp_output_token が putc (c, fp) を、trace_include が
 * putc ('.', stderr) を使う。 */
#include <stdio.h>

int main(void) {
  char *s;
  int i;

  s = "putc";
  i = 0;
  while (s[i]) { putc(s[i], stdout); i = i + 1; }
  putc('\n', stdout);

  /* stderr へも書く (kernel24 はどちらもコンソールへ出す) */
  putc('e', stderr);
  putc('\n', stderr);

  /* 返り値は書いた文字である */
  if (putc('x', stdout) != 'x') { printf("rv-bad\n"); return 1; }
  putc('\n', stdout);

  /* fputc と同じ結果になること */
  fputc('f', stdout);
  putc('p', stdout);
  putc('\n', stdout);
  return 0;
}
