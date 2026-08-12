/* ilp32 の呼出し規約: 構造体 (その 3)。
 * 語 1 本・語 2 本・語 2 本を超えるもの (番地渡し) の受け渡しと返却，
 * および整数と混ぜた並びを通す。memcpy などは libc が無いのでここに置く。 */

void *memcpy(void *d, const void *s, unsigned n)
{ char *a = d; const char *b = s; unsigned i; for (i = 0; i < n; i++) a[i] = b[i]; return d; }
void *memmove(void *d, const void *s, unsigned n)
{ char *a = d; const char *b = s; unsigned i;
  if (a <= b) { for (i = 0; i < n; i++) a[i] = b[i]; }
  else { for (i = n; i > 0; i--) a[i-1] = b[i-1]; }
  return d; }
void *memset(void *d, int c, unsigned n)
{ char *a = d; unsigned i; for (i = 0; i < n; i++) a[i] = (char)c; return d; }

static void putc_(int c) { *(volatile char *)0x10000000 = c; }
static void puthex(unsigned v) { int i; for (i=28;i>=0;i=i-4){int d=(v>>i)&15;putc_(d<10?'0'+d:'a'+d-10);} }

struct S2 { int a, b; };
struct S4 { int a, b, c, d; };
struct S1 { char c; };

struct S2 mk2(int a, int b) { struct S2 s; s.a = a; s.b = b; return s; }
int use2(struct S2 s) { return s.a * 100 + s.b; }
struct S4 mk4(int a) { struct S4 s; s.a=a; s.b=a*2; s.c=a*3; s.d=a*4; return s; }
int use4(struct S4 s) { return s.a + s.b + s.c + s.d; }
struct S1 mk1(int c) { struct S1 s; s.c = (char)c; return s; }
int use1(struct S1 s) { return s.c; }
int mixst(int x, struct S2 s, int y) { return x + s.a * 10 + s.b * 100 + y * 1000; }

void cmain(void) {
    puthex(use2(mk2(7, 9)));            putc_(':');
    puthex(use4(mk4(5)));               putc_(':');
    puthex(use1(mk1(65)));              putc_(':');
    puthex(mixst(1, mk2(2, 3), 4));     putc_('\n');
    *(volatile int *)0x100000 = 0x5555;
    for (;;) ;
}
