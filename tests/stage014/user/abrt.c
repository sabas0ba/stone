/* assert の失敗の検査。式の文字列が stderr に出て exit(1) する */
#include <stdio.h>
#include <assert.h>

int main(void) {
  assert(1 == 1);
  assert(2 < 1);
  puts("not reached");
  return 0;
}
