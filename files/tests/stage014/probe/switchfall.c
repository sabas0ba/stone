/* switch の落下・中央の default・ラベルの連続 */
int putc(int c);
int cls(int c) {
  switch (c) {
  case 'a':
  case 'b':
    return 1;
  default:
    return 9;
  case 'z':
    return 3;
  }
}
int main(void) {
  putc('0' + cls('a'));
  putc('0' + cls('z'));
  putc('0' + cls('q'));
  putc('\n');
  return 0;
}
