// 関数ポインタと static のリンケージ
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
int add2(int a, int b) { return a + b; }
int mul2(int a, int b) { return a * b; }
int neg1(int a) { return 0 - a; }

typedef int (*binop)(int, int);
int (*gp)(int, int);
binop table[3];

// 高階関数: 演算を引数で受ける
int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int applyu(binop f, int a, int b) { return f(a, b); }

// static は翻訳単位に閉じる (helper.c にも同名の static がある)
static int hidden(int v) { return v + 100; }
int callhidden(int v) { return hidden(v); }
int fromother(int v);

int main() {
  int (*p)(int, int);
  int (*u)(int);
  int i;
  p = add2;
  pn(p(3, 4));                 // 7
  p = mul2;
  pn(p(3, 4));                 // 12
  pn((*p)(5, 6));              // 30
  gp = add2;
  pn(gp(10, 20));              // 30
  u = neg1;
  pn(u(9));                    // -9
  pn(apply(add2, 2, 3));       // 5
  pn(apply(mul2, 2, 3));       // 6
  pn(applyu(add2, 8, 8));      // 16
  table[0] = add2;
  table[1] = mul2;
  table[2] = add2;
  for (i = 0; i < 3; i++) pn(table[i](2, 5));   // 7 10 7
  pn(callhidden(1));           // 101
  pn(fromother(1));            // 201 (別の翻訳単位の同名 static)
  putc(10);
  return 0;
}
