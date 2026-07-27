int g2[3][4];
char cm[2][5];
char ob[16];
int pn(int v) {
  int n;
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n++; v /= 10; }
  while (n > 0) { n--; putc(ob[n]); }
  putc(' ');
  return 0;
}
int sumrow(int *r, int n) {
  int i; int s;
  s = 0;
  for (i = 0; i < n; i++) s += r[i];
  return s;
}
int main() {
  int a[2][3];
  int i; int j;
  int t;
  for (i = 0; i < 2; i++)
    for (j = 0; j < 3; j++)
      a[i][j] = i * 10 + j;
  pn(a[0][0]); pn(a[0][2]); pn(a[1][0]); pn(a[1][2]);   // 0 2 10 12
  pn(sizeof(a)); pn(sizeof(a[0])); pn(sizeof(a[0][0])); // 24 12 4
  pn(sumrow(a[1], 3));                                   // 10+11+12 = 33
  for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) g2[i][j] = i + j;
  pn(g2[2][3]);                                          // 5
  pn(sizeof(g2)); pn(sizeof(g2[0]));                     // 48 16
  cm[0][0] = 'a'; cm[1][4] = 'z';
  putc(cm[0][0]); putc(cm[1][4]); putc(' ');
  pn(sizeof(cm)); pn(sizeof(cm[0]));                     // 10 5
  t = 0;
  for (i = 0; i < 2; i++) t += sumrow(a[i], 3);
  pn(t);                                                 // 3 + 33 = 36
  putc(10);
  return 0;
}
