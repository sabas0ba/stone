/* 別の翻訳単位その 1。main から呼ばれる */
#include "mathx.h"

int addx(int a, int b) { return a + b; }

int mulx(int a, int b) {
  int r;
  int i;
  r = 0;
  for (i = 0; i < b; i = i + 1) r = r + a;
  return r;
}
