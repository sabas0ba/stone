/* ポインタ修飾の後置 const (char * const)。zlib の z_errmsg で常用 */
int putc(int c);
char buf[4];
char * const bp = buf;
int f(const char * const *tab) { return tab != 0; }
int main(void) {
  bp[0] = 'a';
  putc(bp[0]);
  putc('0' + f(0));
  putc('\n');
  return 0;
}
