// 第 3 部の 1: 初期化子 (docs/stage010-c89.md 14 章)
//
// 大域は .text に実体を置き，局所はフレーム上へ実行時に書く。
// 検査したいのは値そのものより「実体の配置」である。とくに
// 文字列リテラルを含む初期化子は，実体を後ろへ回さないと記号の値が
// ずれるので，文字列を持つ大域と持たない大域を交互に並べてある。

int gi = 42;
int gneg = -7;
char gc = 'A';
short gs = 300;
unsigned gu = 4000000000;
int gt[] = {1, 2, 3, 4};        // 要素数は初期化子から決まる
int gz[5] = {9, 8};             // 足りない分は 0
char gstr[] = "hello";          // 終端の NUL も含めて 6 バイト
char *gp = "world";             // ポインタ。実体は初期化子の後ろへ
int gmid = 5;                   // 文字列の実体を跨いだ後も配置が正しいこと
int *gq = &gmid;                // 他の大域のアドレス
char *gnames[] = {"a1", "b22", "c333"};   // 文字列の配列
enum { EK = 77 };
int ge = EK;                    // 列挙定数
int addx(int a) { return a + 1; }
int (*gf)(int) = addx;          // 関数のアドレス
static int gstat = 11;          // static でも .text へ置く
int gmd[2][2] = {1, 2, 3, 4};   // 多次元は平らに並べる
char gcm[2][3] = {'a', 'b', 'c', 'd', 'e', 'f'};

char ob[16];
int pn(int v) {
  int n;
  if (v < 0) { putc('-'); v = -v; }
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n++; v /= 10; }
  while (n > 0) { n--; putc(ob[n]); }
  putc(' ');
  return 0;
}
int shows(char *s) { while (*s) { putc(*s); s++; } putc(' '); return 0; }

int main() {
  int a = 5;                    // 局所のスカラは代入式と同じ
  int b = a * 3;                // 直前の局所を参照できる
  char lc = 'z';
  int lt[4] = {7, 8};           // 局所の配列。残りは 0 で埋める
  char ls[6] = "hi";
  int lmd[2][3] = {9, 8, 7, 6};
  int i;
  pn(gi); pn(gneg); pn(gc); pn(gs); pn(gu >> 28);
  for (i = 0; i < 4; i++) pn(gt[i]);
  for (i = 0; i < 5; i++) pn(gz[i]);
  shows(gstr); shows(gp);
  pn(gmid); pn(*gq);
  for (i = 0; i < 3; i++) shows(gnames[i]);
  pn(ge); pn(gf(10)); pn(gstat);
  pn(sizeof(gt)); pn(sizeof(gstr)); pn(sizeof(gnames));
  pn(a); pn(b); pn(lc);
  for (i = 0; i < 4; i++) pn(lt[i]);
  shows(ls);
  for (i = 0; i < 2; i++) for (b = 0; b < 2; b++) pn(gmd[i][b]);
  for (i = 0; i < 2; i++) for (b = 0; b < 3; b++) putc(gcm[i][b]);
  putc(' ');
  for (i = 0; i < 2; i++) for (b = 0; b < 3; b++) pn(lmd[i][b]);
  pn(sizeof(gmd)); pn(sizeof(gcm)); pn(sizeof(lmd));
  putc(10);
  return 0;
}
