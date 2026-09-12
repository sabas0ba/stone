/* 前処理: #elif と算術式の評価 */
#define N 3
int putc(int c);
int main(void) {
#if N == 1
  putc('a');
#elif N == 2
  putc('b');
#elif N * 2 == 6
  putc('c');
#else
  putc('d');
#endif
  putc('\n');
  return 0;
}
