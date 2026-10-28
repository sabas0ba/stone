// 局所の構造体を式で初期化する (cc15o)。1 検査 1 文字を出し，落ちた検査は大文字。
// 期待出力: abcdef
int putc(int c);
struct P { int a; int b; };
union U { int i; char c[4]; };
struct P mk(int a, int b) { struct P r; r.a = a; r.b = b; return r; }
struct P gp1;
union U gu1;
void dirty(void) { int a[24]; int i; for (i=0;i<24;i=i+1) a[i]=0x5a5a5a5a; if (a[0]==0) putc('!'); }
int t_local(void)   { struct P s; struct P d = s; return d.a; }        /* 局所から */
int t_global(void)  { struct P d = gp1; return d.a + d.b; }            /* 大域から */
int t_ptr(struct P *p) { struct P d = *p; return d.a + d.b; }          /* 間接参照から */
int t_call(void)    { struct P d = mk(3, 4); return d.a + d.b; }       /* 呼出しから */
int t_union(void)   { union U d = gu1; return d.i; }                   /* 共用体 */
int t_brace(void)   { struct P d = {5, 6}; return d.a + d.b; }         /* 波括弧 (対照) */
int t_scalar(void)  { int d = gp1.a; return d; }                       /* スカラ (対照) */
int main() {
  gp1.a = 1; gp1.b = 2; gu1.i = 9;
  dirty(); if (t_global() == 3) putc('a'); else putc('A');
  dirty(); if (t_ptr(&gp1) == 3) putc('b'); else putc('B');
  dirty(); if (t_call() == 7) putc('c'); else putc('C');
  dirty(); if (t_union() == 9) putc('d'); else putc('D');
  dirty(); if (t_brace() == 11) putc('e'); else putc('E');
  dirty(); if (t_scalar() == 1) putc('f'); else putc('F');
  putc('\n');
  return 0;
}
