/* sizeof の各形と，配列要素数の常套句 */
int putc(int c);
struct s { int a; char b; };
int arr[7];
int main(void) {
  putc('0' + (int)(sizeof(arr) / sizeof(arr[0])));
  putc('0' + (int)sizeof(struct s));
  putc('0' + (int)sizeof(int));
  putc('\n');
  return 0;
}
