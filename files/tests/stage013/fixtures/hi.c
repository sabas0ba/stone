#define GREET "built in guest\n"
int putc(int c);
int main(void) {
  char *s;
  int i;
  s = GREET;
  for (i = 0; s[i]; i++) putc(s[i]);
  return 7;
}
