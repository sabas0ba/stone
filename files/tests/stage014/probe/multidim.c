/* 多次元配列を関数へ渡す */
int putc(int c);
int m[2][3];
int pick(int t[2][3], int i, int j) { return t[i][j]; }
int main(void) {
  m[1][2] = 6;
  putc('0' + pick(m, 1, 2));
  putc('\n');
  return 0;
}
