/* 文字列へ書く側 (sprintf / snprintf / vsnprintf) を，ホストの処理系と
 * 突き合わせる (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.4)。
 *
 * `printfmt` が見るのは**書いた中身**で，ここが見るのは**返り値と
 * 切り詰め**である。呼び手は返り値で容れ物の要否を決めるので，ここが
 * 違うと切り詰めに気づかないまま先へ進む。
 *
 * `snprintf` は C99 だが，返り値を「入り切ったとしたら書いた長さ」と
 * 定める規則も C99 のものである。我々の libc はその規則で実装している
 * (stage012 第 4 部) ので，同じ規則のホストと突き合わせられる。 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int vs(char *buf, size_t size, char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

/* 終端の後ろも見る。切り詰めたとき、容れ物のどこまでを触ったかが判る */
static void dump(char *tag, char *b, int n) {
  int i;
  printf("%s", tag);
  for (i = 0; i < n; i = i + 1) {
    if (b[i] == 0) printf(" .");
    else printf(" %c", b[i]);
  }
  printf("\n");
}

int main(void) {
  char b[32];
  int n;

  n = sprintf(b, "%d-%s", 42, "ab");
  printf("sprintf %d [%s]\n", n, b);

  n = sprintf(b, "");
  printf("empty   %d [%s]\n", n, b);

  /* 入り切る */
  memset(b, '#', sizeof b);
  n = snprintf(b, 16, "%d-%s", 42, "ab");
  printf("fit     %d [%s]\n", n, b);
  dump("fitb   ", b, 9);

  /* 切り詰め。**返り値は入り切ったとしたらの長さ**で，容れ物の大きさ
   * ではない。書くのは size-1 バイトまでで，必ず終端する */
  memset(b, '#', sizeof b);
  n = snprintf(b, 4, "%d-%s", 42, "ab");
  printf("trunc   %d [%s]\n", n, b);
  dump("truncb ", b, 8);

  /* size == 1 は終端だけ */
  memset(b, '#', sizeof b);
  n = snprintf(b, 1, "abc");
  printf("one     %d\n", n);
  dump("oneb   ", b, 4);

  /* size == 0 は 1 バイトも触らない。長さだけ返す */
  memset(b, '#', sizeof b);
  n = snprintf(b, 0, "abc");
  printf("zero    %d\n", n);
  dump("zerob  ", b, 4);

  memset(b, '#', sizeof b);
  n = vs(b, 8, "%s=%d", "k", 7);
  printf("vs      %d [%s]\n", n, b);
  memset(b, '#', sizeof b);
  n = vs(b, 4, "%s=%d", "key", 7);
  printf("vstrunc %d [%s]\n", n, b);
  return 0;
}
