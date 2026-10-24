/* sh.c --- 行指向の最小シェル (Stage 13 第 1 部)
 *
 * 設計は docs/stage013-tools.md 3.6。1 行を読み，空白で語に分割し，
 * 最初の語を sfs 内のファイル名として spawn する。`< name` / `> name` は
 * 子の標準入出力のつなぎ替えとして取り出す (語の前後に空白が要る)。
 *
 * 組込みは exit と echo の 2 つ。パイプ・変数・引用符・グロブは持たない。
 * 入力の終わりは EOT (0x04) か，結び先ファイルの EOF で表す。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int main(void) {
  char line[256];
  char *av[9];
  int ac;
  char *in;
  char *out;
  char *p;
  char *t;
  int mode;                     /* 0 = 通常, 1 = `<` の次, 2 = `>` の次 */
  int i;
  int r;

  for (;;) {
    fputs("$ ", stdout);
    if (fgets(line, 256, stdin) == NULL) return 0;
    if (strchr(line, 4) != NULL) return 0;      /* EOT */

    /* 語に分割する。区切りは SP / TAB (行末の改行も落とす) */
    ac = 0;
    in = NULL;
    out = NULL;
    mode = 0;
    p = line;
    for (;;) {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == 0) break;
      t = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
      if (*p) { *p = 0; p++; }
      if (mode == 1) { in = t; mode = 0; }
      else if (mode == 2) { out = t; mode = 0; }
      else if (strcmp(t, "<") == 0) mode = 1;
      else if (strcmp(t, ">") == 0) mode = 2;
      else if (ac < 8) { av[ac] = t; ac = ac + 1; }
    }
    if (mode != 0) { fputs("sh: syntax\n", stdout); continue; }
    if (ac == 0) continue;
    av[ac] = NULL;

    /* 組込み */
    if (strcmp(av[0], "exit") == 0) {
      if (ac > 1) return atoi(av[1]);
      return 0;
    }
    if (strcmp(av[0], "echo") == 0) {
      for (i = 1; i < ac; i++) {
        fputs(av[i], stdout);
        if (i + 1 < ac) putchar(' ');
      }
      putchar('\n');
      continue;
    }

    r = spawn(av[0], av, in, out);
    if (r < 0) printf("sh: %s: errno %d\n", av[0], errno);
    else if (r != 0) printf("? %d\n", r);
  }
}
