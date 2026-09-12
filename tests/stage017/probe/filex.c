/* stdio のファイル層を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.4)。
 *
 * ここは **libc と我々の OS (カーネルのファイル系) が一緒に効く**
 * 場所である。5.1 で zlib の `example.c` を通したとき「`gz*` の系統は
 * ファイルを開いて読み書きするので libc のファイル層まで一緒に測れる」
 * と書いた部分を，正面から測る。
 *
 * 見るのは縁である —— 終端で切れた行，容れ物が 1 バイトしかない
 * `fgets`，読めた分が半端な `fread`，押し戻したあとの位置。GCC の
 * `configure` と生成器はどちらもテキストを行で読むので，ここが違うと
 * **読めたつもりで別のものを読む**。
 *
 * ファイルは走らせた場所に作る。ホスト側は tmp の下で走らせる
 * (tools/diff17.sh)。 */
#include <stdio.h>
#include <string.h>

/* 改行を含む文字列は，見えるようにして 1 行で出す */
static void pstr(char *tag, char *s) {
  int i;
  if (s == 0) { printf("%s (null)\n", tag); return; }
  printf("%s [", tag);
  for (i = 0; s[i]; i = i + 1) {
    if (s[i] == '\n') printf("\\n");
    else printf("%c", s[i]);
  }
  printf("]\n");
}

int main(void) {
  FILE *f;
  char b[64];
  int n;
  int c;
  long p;

  /* ---- 書く ---- */
  f = fopen("fx.txt", "w");
  printf("wopen  %d\n", f != 0);
  if (f == 0) return 1;
  n = (int)fwrite("hello\nworld", 1, 11, f);
  printf("fwrite %d\n", n);
  printf("fputs  %d\n", fputs("!", f) >= 0);   /* 値そのものは処理系依存 */
  printf("wclose %d\n", fclose(f));

  /* ---- 行で読む ---- */
  f = fopen("fx.txt", "r");
  printf("ropen  %d\n", f != 0);
  if (f == 0) return 1;
  memset(b, '#', sizeof b);
  pstr("fgets1", fgets(b, 64, f));       /* 改行まで。改行も入る */
  memset(b, '#', sizeof b);
  pstr("fgets2", fgets(b, 64, f));       /* 終端で切れた行。改行は無い */
  memset(b, '#', sizeof b);
  pstr("fgets3", fgets(b, 64, f));       /* もう無い */
  printf("eof1   %d\n", feof(f) != 0);
  printf("err1   %d\n", ferror(f) != 0);
  fclose(f);

  /* 容れ物が足りない fgets。n-1 バイトで切り，終端を書く */
  f = fopen("fx.txt", "r");
  memset(b, '#', sizeof b);
  pstr("small3", fgets(b, 3, f));
  memset(b, '#', sizeof b);
  pstr("small2", fgets(b, 2, f));
  /* **n == 1 は 1 バイトも読まずに終端だけ書く** (C89 7.9.7.2)。
   * 「1 バイトも読めなかった」とは違う */
  memset(b, '#', sizeof b);
  pstr("one   ", fgets(b, 1, f));
  fclose(f);

  /* ---- 塊で読む ---- */
  f = fopen("fx.txt", "r");
  memset(b, 0, sizeof b);
  n = (int)fread(b, 1, 5, f);
  printf("fread1 %d\n", n);
  pstr("read1 ", b);
  memset(b, 0, sizeof b);
  /* 12 バイト要求して 7 バイトしかない。**員の数で数える** */
  n = (int)fread(b, 4, 3, f);
  printf("fread2 %d\n", n);
  pstr("read2 ", b);
  printf("eof2   %d\n", feof(f) != 0);
  printf("tell2  %ld\n", ftell(f));
  fclose(f);

  /* ---- 位置 ---- */
  f = fopen("fx.txt", "r");
  printf("tell0  %ld\n", ftell(f));
  printf("seek1  %d\n", fseek(f, 6, SEEK_SET));
  printf("tell1  %ld\n", ftell(f));
  printf("getc1  %c\n", fgetc(f));
  printf("seek2  %d\n", fseek(f, 2, SEEK_CUR));
  printf("tell3  %ld\n", ftell(f));
  printf("seek3  %d\n", fseek(f, 0, SEEK_END));
  printf("tell4  %ld\n", ftell(f));
  printf("getc2  %d\n", fgetc(f));       /* 終端。EOF (-1) */
  printf("eof3   %d\n", feof(f) != 0);
  /* fseek は終端の印を落とす (C89 7.9.9.2) */
  printf("seek4  %d\n", fseek(f, 0, SEEK_SET));
  printf("eof4   %d\n", feof(f) != 0);
  fclose(f);

  /* ---- 押し戻し ---- */
  f = fopen("fx.txt", "r");
  c = fgetc(f);
  printf("getc3  %c\n", c);
  printf("unget  %d\n", ungetc(c, f) == c);
  printf("tellu  %ld\n", ftell(f));      /* 押し戻した分だけ手前 */
  printf("getc4  %c\n", fgetc(f));
  printf("tella  %ld\n", ftell(f));
  fclose(f);

  /* ---- 追記 ---- */
  f = fopen("fx.txt", "a");
  printf("aopen  %d\n", f != 0);
  fputs("XY", f);
  fclose(f);
  f = fopen("fx.txt", "r");
  memset(b, 0, sizeof b);
  n = (int)fread(b, 1, 60, f);
  printf("afread %d\n", n);
  pstr("after ", b);
  fclose(f);

  /* ---- 消す ---- */
  printf("remove %d\n", remove("fx.txt"));
  f = fopen("fx.txt", "r");
  printf("gone   %d\n", f == 0);
  printf("rmagn  %d\n", remove("fx.txt") != 0);   /* 無いものは消せない */
  printf("nofile %d\n", fopen("nosuch.txt", "r") == 0);
  return 0;
}
