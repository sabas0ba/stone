/* OS 上で動く最初のユーザプログラム (docs/stage012-os.md 8 章 第 2 部)
 *
 * putc は 'E' 前置部が write(1, ...) の 1 バイト版として提供する。
 * argv[0] (= 自分の名前) を出し，終了コード 3 を返す。
 */
int putc(int c);

int main(int argc, char **argv) {
  char *p;
  int i;
  putc('h'); putc('i'); putc(' ');
  putc('0' + argc);
  putc(' ');
  p = argv[0];
  for (i = 0; p[i]; i++) putc(p[i]);
  putc('\n');
  return 3;
}
