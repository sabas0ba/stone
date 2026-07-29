/* string.h の値の照合 (docs/stage011-libc.md 5 章)
 *
 * 境界を落とさない:
 *   - memmove の重なり (前へ・後ろへ・完全一致)
 *   - strncpy の詰め物 (n に満たないとき NUL で埋まること) と
 *     非終端 (n 以内に NUL が無ければ終端しないこと)
 *   - strchr(s, 0) が終端を指すこと
 *   - memcmp / strcmp が unsigned char として比較すること
 *     (0x80 以上を含む文字列で符号付き比較と結果が変わる)
 *
 * リンクするのは string.o だけである (リンクの単位の検査を兼ねる)。
 */
#include <stddef.h>
#include <string.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

char b1[32];
char b2[32];

int main() {
  char *p;

  /* memcpy */
  memset(b1, 0, 32);
  p = (char *)memcpy(b1, "abcde", 6);
  ok(p == b1);
  ok(strcmp(b1, "abcde") == 0);

  /* memmove: 前へ (d < s) */
  memset(b1, 0, 32);
  strcpy(b1, "abcdef");
  memmove(b1, b1 + 2, 5);
  ok(strcmp(b1, "cdef") == 0);

  /* memmove: 後ろへ (d > s) */
  memset(b1, 0, 32);
  strcpy(b1, "abcdef");
  memmove(b1 + 2, b1, 5);
  ok(strcmp(b1, "ababcde") == 0);

  /* memmove: 完全一致 (d == s) */
  memmove(b1, b1, 7);
  ok(strcmp(b1, "ababcde") == 0);

  /* memset */
  p = (char *)memset(b2, 'x', 4);
  ok(p == b2);
  b2[4] = 0;
  ok(strcmp(b2, "xxxx") == 0);

  /* memcmp (unsigned char として比較する) */
  ok(memcmp("abc", "abc", 3) == 0);
  ok(memcmp("abc", "abd", 3) < 0);
  ok(memcmp("abd", "abc", 3) > 0);
  ok(memcmp("a\x7f", "a\x80", 2) < 0);
  ok(memcmp("a", "b", 0) == 0);

  /* strlen */
  ok(strlen("") == 0);
  ok(strlen("hello") == 5);

  /* strcpy */
  p = strcpy(b1, "hi");
  ok(p == b1);
  ok(b1[0] == 'h' && b1[1] == 'i' && b1[2] == 0);

  /* strncpy: n に満たない分は NUL で埋まる */
  memset(b1, 'x', 31);
  b1[31] = 0;
  p = strncpy(b1, "ab", 5);
  ok(p == b1);
  ok(b1[0] == 'a' && b1[1] == 'b');
  ok(b1[2] == 0 && b1[3] == 0 && b1[4] == 0);
  ok(b1[5] == 'x');                     /* n の先には触れない */

  /* strncpy: n 以内に NUL が無ければ終端しない */
  b1[3] = 'z';
  strncpy(b1, "abcdef", 3);
  ok(b1[0] == 'a' && b1[1] == 'b' && b1[2] == 'c');
  ok(b1[3] == 'z');

  /* strcmp / strncmp (unsigned char として比較する) */
  ok(strcmp("", "") == 0);
  ok(strcmp("abc", "abc") == 0);
  ok(strcmp("abc", "abd") < 0);
  ok(strcmp("abc", "ab") > 0);
  ok(strcmp("a\x80", "a\x7f") > 0);
  ok(strncmp("abcde", "abcxx", 3) == 0);
  ok(strncmp("abcde", "abcxx", 4) < 0);
  ok(strncmp("abc", "abc", 10) == 0);   /* NUL で止まる */
  ok(strncmp("xyz", "abc", 0) == 0);

  /* strchr / strrchr (終端の NUL も探索対象に含む) */
  p = "banana";
  ok(strchr(p, 'a') == p + 1);
  ok(strchr(p, 'q') == NULL);
  ok(strchr(p, 0) == p + 6);
  ok(strrchr(p, 'a') == p + 5);
  ok(strrchr(p, 'q') == NULL);
  ok(strrchr(p, 0) == p + 6);

  /* strcat / strncat */
  memset(b1, 0, 32);
  strcpy(b1, "foo");
  p = strcat(b1, "bar");
  ok(p == b1);
  ok(strcmp(b1, "foobar") == 0);
  strcpy(b1, "foo");
  p = strncat(b1, "barbaz", 3);         /* 高々 n バイト + NUL */
  ok(p == b1);
  ok(strcmp(b1, "foobar") == 0);
  strncat(b1, "qux", 10);               /* n が長くても NUL で止まる */
  ok(strcmp(b1, "foobarqux") == 0);

  /* strstr */
  p = "hello world";
  ok(strstr(p, "world") == p + 6);
  ok(strstr(p, "o w") == p + 4);
  ok(strstr(p, "xyz") == NULL);
  ok(strstr(p, "") == p);               /* 空の針は先頭 */

  /* strspn / strcspn / strpbrk */
  ok(strspn("123abc", "0123456789") == 3);
  ok(strspn("abc", "0123456789") == 0);
  ok(strspn("111", "1") == 3);          /* 全部が集合でも終端で止まる */
  ok(strcspn("abc123", "0123456789") == 3);
  ok(strcspn("abc", "0123456789") == 3);
  ok(strcspn("1abc", "1") == 0);
  p = "abc123";
  ok(strpbrk(p, "0123456789") == p + 3);
  ok(strpbrk(p, "xyz") == NULL);

  putc('\n');
  return 0;
}
