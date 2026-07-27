typedef int myint;
typedef char *str;

enum color { RED, GREEN, BLUE = 10, WHITE };
enum { LONE = 42 };

struct pt { myint x; myint y; };
typedef struct pt point;

union box {
  int i;
  char c[4];
  int *p;
};
union box ub;

struct tagged {
  int kind;
  int a;
  int b;
};
struct tagged tg;

typedef struct pt *pptr;
struct pt origin;

char ob[16];
int pn(int v) {
  int n;
  if (v < 0) { putc('-'); v = -v; }
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n = n + 1; v = v / 10; }
  while (n > 0) { n = n - 1; putc(ob[n]); }
  putc(' ');
  return 0;
}
int shows(str s) {
  while (*s) { putc(*s); s++; }
  return 0;
}
int sum(pptr q) { return q->x + q->y; }

int main() {
  myint a;
  const int b;
  volatile myint c;
  point *pp;
  int i;

  pn(RED); pn(GREEN); pn(BLUE); pn(WHITE); pn(LONE);   // 0 1 10 11 42
  a = 3; c = 4;
  pn(a + c);                                            // 7
  shows("hi ");
  origin.x = 20; origin.y = 22;
  pp = &origin;
  pn(sum(pp));                                          // 42
  pn(sizeof(point)); pn(sizeof(union box)); pn(sizeof(myint));  // 8 4 4
  // union: 同じ場所を別の型で見る
  ub.i = 0;
  ub.c[0] = 'A';
  pn(ub.i);                                             // 65
  ub.i = 0x41424344;
  for (i = 0; i < 4; i++) putc(ub.c[i]);                // DCBA (little endian)
  putc(' ');
  // enum を switch のラベルには使えない (定数式は第 2 部の 2)。値で書く
  switch (GREEN) {
  case 1: shows("g "); break;
  default: shows("? ");
  }
  putc(10);
  return 0;
}
