/* BUFSIZ と setvbuf / setbuf を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 8.11 / stage017/libc25.md)。
 *
 * 我々の FILE は緩衝しない。setvbuf は無緩衝 (_IONBF) の要求にだけ応じ，
 * 緩衝の要求には非 0 (応じられない) を返す。ホスト (glibc) は緩衝の要求にも
 * 応じるので，**そこは突き合わせない** (どちらも C89 7.9.5.6 が認める)。
 *
 * 突き合わせるのは両方で同じ答になるはずの性質である ——
 *   BUFSIZ が C89 の下限 (256) 以上か
 *   無緩衝の要求に応じるか
 *   setbuf の後も書いた中身が変わらないか */
#include <stdio.h>

static void yn(char *tag, int v) { printf("%s %s\n", tag, v ? "y" : "n"); }

int main(void) {
  static char buf[BUFSIZ];
  FILE *f;
  int c;
  int n;
  yn("bufsiz", BUFSIZ >= 256);
  yn("modes", _IOFBF != _IOLBF && _IOLBF != _IONBF && _IOFBF != _IONBF);
  f = fopen("sb_a.txt", "w");
  yn("open", f != 0);
  yn("nbf", setvbuf(f, 0, _IONBF, 0) == 0);
  fputs("abc", f);
  fclose(f);
  f = fopen("sb_b.txt", "w");
  setbuf(f, buf);
  fputs("xyz\n", f);
  fclose(f);
  f = fopen("sb_a.txt", "r");
  n = 0;
  while ((c = fgetc(f)) != EOF) n = n * 256 + c;
  fclose(f);
  yn("a", n == ('a' * 256 + 'b') * 256 + 'c');
  f = fopen("sb_b.txt", "r");
  n = 0;
  while ((c = fgetc(f)) != EOF) n = n + c;
  fclose(f);
  yn("b", n == 'x' + 'y' + 'z' + '\n');
  return 0;
}
