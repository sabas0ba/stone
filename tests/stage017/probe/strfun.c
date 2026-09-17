/* string.h の縁を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 5.3)。
 *
 * 見るのは**普段の使い方では通らない縁**である —— 重なりのある
 * memmove，長さの足りない strncpy の詰め物，空の needle，終端そのものを
 * 探す strchr。我々のソースはこれらを使わないので，自分自身を組む限り
 * 永久に表に出ない (cc15u と同じ理由)。
 *
 * ポインタを返すものは**差 (先頭からの位置)** で出す。番地そのものは
 * 走る場所で変わるので比べられない。返り値の符号だけが定まっている
 * ものは -1 / 0 / 1 へ畳む (C89 は memcmp の値の大きさを定めない)。 */
#include <stdio.h>
#include <string.h>

static int sign(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

static void dump(char *tag, char *b, int n) {
  int i;
  printf("%s", tag);
  for (i = 0; i < n; i = i + 1) printf(" %02x", (unsigned char)b[i]);
  printf("\n");
}

static void at(char *tag, char *p, char *base) {
  if (p == 0) printf("%s null\n", tag);
  else printf("%s %d\n", tag, (int)(p - base));
}

int main(void) {
  char b[16];
  char c[16];
  char *p;

  /* memmove は重なっていても正しい。memcpy は重なりを定めないので
   * ここでは試さない (試すと「どちらが正しい」が言えない) */
  strcpy(b, "abcdefgh");
  memmove(b + 2, b, 6);
  dump("mvup  ", b, 9);
  strcpy(b, "abcdefgh");
  memmove(b, b + 2, 6);
  dump("mvdn  ", b, 9);
  strcpy(b, "abcdefgh");
  memmove(b, b, 8);
  dump("mvsame", b, 9);

  memset(b, 0x41, 4);
  memset(b + 4, 0, 1);
  dump("memset", b, 5);

  printf("memcmp %d %d %d\n",
         sign(memcmp("abc", "abd", 3)),
         sign(memcmp("abd", "abc", 3)),
         sign(memcmp("abc", "abd", 2)));
  /* 上位ビットの立った文字は unsigned char として比べる (C89 7.11.4.1) */
  b[0] = (char)0x80; b[1] = 0;
  c[0] = 0x01; c[1] = 0;
  printf("memhi  %d\n", sign(memcmp(b, c, 1)));
  printf("strhi  %d\n", sign(strcmp(b, c)));
  printf("memc0  %d\n", sign(memcmp("x", "y", 0)));

  printf("strcmp %d %d %d\n",
         sign(strcmp("abc", "abd")), sign(strcmp("abc", "abc")),
         sign(strcmp("abcd", "abc")));
  printf("strncm %d %d\n",
         sign(strncmp("abc", "abd", 2)), sign(strncmp("abc", "abd", 3)));

  /* strncpy: src が短ければ n まで 0 で詰める。長ければ**終端を書かない** */
  memset(b, 0x7e, 12);
  strncpy(b, "ab", 6);
  dump("ncpypd", b, 8);
  memset(b, 0x7e, 12);
  strncpy(b, "abcdefgh", 4);
  dump("ncpycu", b, 6);

  /* strncat は必ず終端を書く (n は src から取る上限であって全長ではない)。
   * 終端より後ろも見るので，毎回同じ地から始める */
  memset(b, 0x7e, 12);
  strcpy(b, "ab");
  strncat(b, "cdef", 2);
  dump("ncat  ", b, 6);
  memset(b, 0x7e, 12);
  strcpy(b, "ab");
  strncat(b, "cd", 8);
  dump("ncatlo", b, 6);

  p = "hello world";
  at("strchr", strchr(p, 'o'), p);
  at("strrch", strrchr(p, 'o'), p);
  at("chrnul", strchr(p, 0), p);      /* 終端そのものを指す */
  at("chrmis", strchr(p, 'z'), p);
  at("strstr", strstr(p, "lo w"), p);
  at("strempt", strstr(p, ""), p);    /* 空の needle は先頭 */
  at("strmis", strstr(p, "wordl"), p);
  at("memchr", memchr(p, 'w', 11), p);
  at("memmis", memchr(p, 'z', 11), p);
  at("memn0 ", memchr(p, 'h', 0), p);

  printf("strlen %d %d\n", (int)strlen(""), (int)strlen("abcd"));
  printf("strspn %d %d %d\n",
         (int)strspn("abcde", "abc"), (int)strspn("abcde", "xyz"),
         (int)strspn("", "abc"));
  printf("strcsp %d %d\n",
         (int)strcspn("abcde", "dc"), (int)strcspn("abcde", "xyz"));
  at("strpbr", strpbrk(p, "ow"), p);
  at("pbrmis", strpbrk(p, "xyz"), p);
  return 0;
}
