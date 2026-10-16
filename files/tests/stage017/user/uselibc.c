/* libc.a だけを頼りにリンクされるプログラム。
 *
 * /lib には走り時の下働き (rt64 / rtfp) しか置かない配置で組むので，
 * ここで呼ぶ printf / strcpy / strlen / malloc は**すべて書庫から
 * 引かれる**。1 つでも引けなければリンクが落ちる。
 *
 * 世代の異なる libc を混ぜていないことも見たいので，libc の別々の
 * 翻訳単位に散った関数を意図して並べてある —— printf は posix/stdio，
 * strcpy / strlen は src/string，malloc は posix/morecore である。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  char *p;
  p = malloc(32);
  if (p == 0) {
    printf("malloc failed\n");
    return 1;
  }
  strcpy(p, "libc.a");
  printf("linked against %s (%d)\n", p, (int)strlen(p));
  return 0;
}
