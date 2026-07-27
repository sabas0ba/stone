int shared;
char ob[16];

int show(int v) {
  int n;
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n++; v /= 10; }
  while (n > 0) { n--; putc(ob[n]); }
  putc(' ');
  return 0;
}

int addup(int a, int b) { return a + b; }
