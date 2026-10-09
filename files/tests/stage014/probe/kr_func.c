/* K&R 形式の関数定義。1989 年より前に書かれた C はこの形である */
int putc(int c);
int add(a, b)
int a;
int b;
{
  return a + b;
}
int main(void) {
  putc('0' + add(3, 4));
  putc('\n');
  return 0;
}
