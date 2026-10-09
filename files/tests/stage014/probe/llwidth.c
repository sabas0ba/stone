/* long long の幅。C89 には無いが現実の C では広く現れる。
 * 通ってしまい，かつ 32 bit として扱われるなら「静かに壊れる」 */
int putc(int c);
int main(void) {
  putc('0' + (int)sizeof(long long));
  putc('\n');
  return 0;
}
