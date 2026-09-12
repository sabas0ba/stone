/* 前処理: defined 演算子・入れ子の #if・## と # */
#define A 1
#define CAT(x, y) x ## y
#define STR(x) #x
int putc(int c);
int main(void) {
#if defined(A) && !defined(B)
  int CAT(v, 1);
  char *s;
  v1 = 7;
  s = STR(ok);
  putc('0' + v1);
  putc(s[0]);
  putc(s[1]);
  putc('\n');
#endif
  return 0;
}
