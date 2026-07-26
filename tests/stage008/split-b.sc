int emit(int v) {
  char *s;
  s = "val=";
  while (*s) { putc(*s); s = s + 1; }
  if (v > 9) putc('0' + v / 10);
  putc('0' + v % 10);
  return 0;
}
