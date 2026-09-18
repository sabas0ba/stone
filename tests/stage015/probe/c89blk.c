/* C89 が許すのに拒んでいた 5 つの形を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.3 の 1・2・3・5・6 である。
 *
 * **通ることだけでは足りない。** 5 つのうち 2 は値の問題である ——
 * ビットフィールドの ++ は幅からはみ出た桁を捨て，隣のフィールドを
 * 壊してはならない。3 と 5 と 6 は名前の解決の問題で，違う実体に
 * 結びつけば値が変わる。だからどれも**値を出して**突き合わせる。
 *
 * 1 は `ansidecl.h` の VA_OPEN が複文の先頭に展開する形で，可変長引数を
 * 使う単位すべてに効く。ここでは va_list を使わずに形だけを置く ——
 * 前置部だけで走る側には stdarg が無い。 */
int putc(int c);

static void pn(int v) {
  char b[16];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void nl(void) { putc('\n'); }

/* 3. 関数名を括弧で囲む定義 (C89 6.5.4 の「( 宣言子 )」)。
 *    GCC の hashtab.c がマクロとの衝突を避けるためこう書く */
static int (twice)(int x) { return x * 2; }

/* ビットフィールドを隣り合わせに並べる。++ が隣を壊さないことを見る */
struct bits {
  unsigned a : 3;
  unsigned b : 5;
  unsigned c : 1;
  unsigned d : 8;
};

int main(void) {
  struct bits v;
  int i;

  /* 1. 複文の中のタグだけの宣言。VA_OPEN が展開するのと同じ形 */
  {
    struct Qdmy;
    pn(1);
  }

  /* 2. ビットフィールドへの ++ / --。**幅で折り返す** */
  v.a = 1; v.b = 2; v.c = 0; v.d = 200;
  v.a++;
  pn((int)v.a); pn((int)v.b); pn((int)v.c); pn((int)v.d);
  /* 3 bit の 7 に 1 を足せば 0 になる。隣は動かない */
  v.a = 7;
  v.a++;
  pn((int)v.a); pn((int)v.b); pn((int)v.c); pn((int)v.d);
  /* 0 から 1 を引けば 7 である */
  v.a = 0;
  v.a--;
  pn((int)v.a); pn((int)v.b); pn((int)v.c); pn((int)v.d);
  nl();

  /* 後置は変更前の値，前置は変更後の値 */
  v.d = 10;
  pn((int)(v.d++)); pn((int)v.d);
  pn((int)(++v.d)); pn((int)v.d);
  v.d = 0;
  pn((int)(v.d--)); pn((int)v.d);       /* 8 bit なので 255 へ折り返す */
  nl();

  /* 1 bit のフィールドは 0 と 1 を往復する */
  v.c = 0;
  for (i = 0; i < 3; i = i + 1) { v.c++; pn((int)v.c); }
  nl();

  /* 3. 括弧で囲んだ名前で定義した関数を呼ぶ */
  pn(twice(21));
  {
    /* 括弧で囲んだ名前の宣言 (変数の側) */
    int (w);
    w = 5;
    pn(w);
  }
  nl();

  /* 5. block scope の typedef。内側で宣言し，抜ければ見えなくなる */
  {
    typedef int small;
    small s;
    s = 7;
    pn(s);
  }
  {
    /* 外では同じ名前をふつうの変数に使える */
    int small;
    small = 9;
    pn(small);
  }
  nl();

  /* 6. block scope の extern。実体は外にある。
   *
   * **名前が見えるのはこの複文の中だけである** (C89 6.1.2.1)。
   * 外で使えばホストは 'shared' undeclared で拒む —— 我々は
   * 記号表が 1 つしかないので受けてしまう。**C89 より緩い**が，
   * 正しいソースを誤って拒むことはない (stage015/cc15ad.md)。
   * ここでは C89 が定める範囲だけを測る */
  {
    extern int shared;
    pn(shared);
    shared = shared + 1;
    pn(shared);
  }
  {
    extern int shared;
    pn(shared);                         /* 別の複文から同じ実体が見える */
  }
  nl();
  return 0;
}

/* extern が指す実体。**宣言より後ろに置く** —— 前にあると
 * 「たまたま先に見えていた」だけかもしれない */
int shared = 40;
