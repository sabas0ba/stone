/* 複数の翻訳単位と書庫の検査。
 *
 * addx / mulx / lenx / catx は書庫 (libx.a) の中にある。
 * printf / strcat は /lib の中にある。**両方が引けること**を見る。
 *
 * VERSION は -D で外から与える。定義されていなければ翻訳が落ちる
 * ので，-D が効いていることが結果に出る */
#include <stdio.h>
#include "mathx.h"
#include "strx.h"

int main(void) {
  char b[64];

  printf("add %d\n", addx(20, 22));
  printf("mul %d\n", mulx(6, 7));
  printf("len %d\n", lenx("0123456789"));

  b[0] = 0;
  catx(b, "ab");
  catx(b, "cd");
  printf("cat %s\n", b);

  printf("ver %d\n", VERSION);
#ifdef NOPE
  printf("nope should not appear\n");
#endif
  return 0;
}
