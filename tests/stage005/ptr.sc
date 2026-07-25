// 仕様 2.2/2.5 節: ポインタ・配列・アドレス取得・ポインタ演算・文字列。
int a[5];
char msg[6];
int sum(int *p, int n) {
  int s;
  s = 0;
  while (n) { s = s + *p; p = p + 1; n = n - 1; }
  return s;
}
int main() {
  int i;
  int *p;
  char *s;
  i = 0;
  while (i < 5) { a[i] = i + 1; i = i + 1; }
  if (sum(a, 5) == 15) putc(111); else putc(88);
  p = &a[3];
  *p = 42;
  if (a[3] == 42) putc(111); else putc(88);
  if (p - a == 3) putc(111); else putc(88);
  s = "hi";
  msg[0] = s[0] - 32;
  msg[1] = *(s + 1) - 32;
  msg[2] = 0;
  putc(msg[0]);
  putc(msg[1]);
  putc(10);
  return 0;
}
