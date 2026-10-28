/* 64 bit の値を条件にそのまま使う (第 3 部 その 2)。
 * 上位語だけが立つ値 (下位語 0) が真になることが要点。cc15e までは拒んだ */
int putc(int c);
int t(long long v) { if (v) return 1; return 0; }
int nz(unsigned long long v) { return !v; }
int main(void) {
  unsigned long long h;
  int n;
  h = 0x100000000ULL;          /* 下位語は 0 */
  putc('0' + t(h));
  putc('0' + t(0LL));
  putc('0' + t(-1LL));
  putc('0' + nz(h));
  putc('0' + nz(0ULL));
  if (h && 1) putc('a'); else putc('b');
  if (h || 0) putc('c'); else putc('d');
  n = 0;
  while (h) { h = h >> 8; n = n + 1; }
  putc('0' + n);
  putc('\n');
  return 0;
}
