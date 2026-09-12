// 構造体の配置の検査 (cc15p。docs/stage015-tcc.md 14 章)。
// 期待値は riscv32-tcc に静的表明で確かめたものである
// (tests/stage015/probe/layout-oracle.c)。
// 全部合っていれば "ok" の 1 行だけを出す。合わなければその項目を
// "名前=実測!期待" の形で並べる。
int putc(int c);
int bad;
void hex(unsigned v) {
  int i; int d;
  i = 28;
  while (i >= 0) { d = (v >> i) & 15; putc(d < 10 ? '0'+d : 'a'+d-10); i = i - 4; }
}
void sh(char *t, unsigned got, unsigned want) {
  int i;
  i = 0;
  while (t[i]) { putc(t[i]); i = i + 1; }
  putc('=');
  hex(got);
  if (got != want) { putc('!'); hex(want); }
  putc(10);
}

void ck(char *t, unsigned got, unsigned want) {
  if (got != want) { bad = 1; sh(t, got, want); }
}
struct SA { unsigned short a:5,b:1,c:1,d:2,e:1,f:1,g:1,h:1,i:1,x:2; };
struct SC { unsigned char  a:3,b:2,c:3; };
struct SI { unsigned       a:5,b:1,c:1,d:2,e:1,f:1,g:1,h:1,i:1,x:2; };
struct SC2 { unsigned char a:3,b:2,c:3,d:1; };
struct M1 { char c; };
struct M2 { char c; short s; };
struct M3 { int v; long long ll; };
struct M4 { char c; long long ll; };
struct M5 { char c; double d; };
struct M6 { long long ll; char c; };
struct M7 { char a; struct M2 m; };
struct M8 { char a; char b[3]; };
struct M3 g3; struct M4 g4; struct M5 g5; struct M7 g7;
int main() {
  ck("SA", sizeof(struct SA), 2);
  ck("SC", sizeof(struct SC), 1);
  ck("SI", sizeof(struct SI), 4);
  ck("SC2", sizeof(struct SC2), 2);
  ck("M1", sizeof(struct M1), 1);
  ck("M2", sizeof(struct M2), 4);
  ck("M3", sizeof(struct M3), 16);
  ck("M3.ll", (unsigned)&g3.ll - (unsigned)&g3, 8);
  ck("M4", sizeof(struct M4), 16);
  ck("M4.ll", (unsigned)&g4.ll - (unsigned)&g4, 8);
  ck("M5", sizeof(struct M5), 16);
  ck("M5.d", (unsigned)&g5.d - (unsigned)&g5, 8);
  ck("M6", sizeof(struct M6), 16);
  ck("M7", sizeof(struct M7), 6);
  ck("M7.m", (unsigned)&g7.m - (unsigned)&g7, 2);
  ck("M8", sizeof(struct M8), 4);
  if (!bad) { putc('o'); putc('k'); putc(10); }
  return 0;
}
