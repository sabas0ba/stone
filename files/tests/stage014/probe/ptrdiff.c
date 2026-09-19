/* ポインタの差・比較と，void * との往復 */
int putc(int c);
int main(void) {
  char buf[8];
  char *p;
  char *q;
  void *v;
  p = buf;
  q = buf + 5;
  v = q;
  if ((q - p) == 5) putc('a');
  if (p < q) putc('b');
  if ((char *)v == q) putc('c');
  putc('\n');
  return 0;
}
