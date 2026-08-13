// cc15m で入れた言語の穴 (docs/stage015-tcc.md 12.6 の表) の単体検査。
// 1 検査 1 文字を出し，落ちた検査だけ 'X' を前置する。裸 ('F') で走る。

typedef void *Fn(void *p, unsigned long n);       // 関数型 typedef

enum ee { E1 = 256 - 1, E2 = 1 << 4, E3 };        // 列挙定数の定数式

struct BF { unsigned int a : 3, b : 5; int w; };  // 宣言子の並び + ビット

struct AN {                                       // 無名メンバ
  int x;
  union {
    struct { int j, k; };
    unsigned long long c;
  };
  int y;
};

typedef struct CT { int t; void *ref; } CT;

static int tent;                                  // 仮定義の重複
static int tent;
static int tini;                                  // 仮定義 + 本定義
static int tini = 42;
 ;                                                // 空宣言

static int tab[((sizeof(long long) + 3) / 4)];    // 定数式の sizeof

static unsigned short offw =
    ((unsigned short)&(((struct AN *)0)->y));     // offsetof + キャスト

int chk(int c, int ok) {
  if (!ok) putc('X');
  putc(c);
  return 0;
}

// ラベルつき文: 条件が偽なら走ってはならない (第 6 部の bad 級)
int lab3(int c, int g) {
  int hit;
  hit = 0;
  if (g) goto lab;
  if (c)
lab:
    hit = hit + 1;
  return hit;
}

int main() {
  unsigned long long u;
  long long s;
  double d;
  float f;
  struct BF bf;
  struct AN an;
  CT ct = { 5 << 20, 0 };                         // 局所の構造体初期化子
  int i;
  char *p;
  unsigned long long big;
  union { double dd; unsigned long long uu; } w;

  // 64 bit の複合代入
  u = 0x100000001ULL;
  u |= 0x0F0ULL;               chk('a', u == 0x1000000F1ULL);
  u += 0x100000000ULL;         chk('b', u == 0x2000000F1ULL);
  u <<= 8;                     chk('c', u == 0x2000000F100ULL);
  u >>= 16;                    chk('d', u == 0x2000000ULL);
  u *= 3;                      chk('e', u == 0x6000000ULL);
  u /= 7;                      chk('f', u == 0xdb6db6ULL);
  s = -16;
  s >>= 2;                     chk('g', s == -4);
  s &= 0xFFLL;                 chk('h', s == 0xFC);

  // 浮動小数点の複合代入
  d = 1.5;
  d += 0.25;                   chk('i', d == 1.75);
  d *= 4.0;                    chk('j', d == 7.0);
  f = 0.5f;
  f += 0.25f;                  chk('k', f == 0.75f);

  // ビットフィールドの複合代入
  bf.a = 1; bf.b = 2; bf.w = 9;
  bf.a |= 4;                   chk('l', bf.a == 5 && bf.b == 2 && bf.w == 9);
  bf.b += 3;                   chk('m', bf.b == 5 && bf.a == 5);

  // ?: の枝が 2 語
  i = 1;
  u = i ? 0x1122334455667788ULL : 0ULL;
  chk('n', u == 0x1122334455667788ULL);
  d = i ? 2.5 : 0.5;           chk('o', d == 2.5);
  s = (u >> 63) ? -1LL : 1LL;  chk('p', s == 1);

  // 無名メンバの配置と読み書き
  an.x = 1; an.j = 2; an.k = 3; an.y = 4;
  chk('q', an.j == 2 && an.k == 3
        && an.c == (((unsigned long long)3 << 32) | 2));
  chk('r', offw == 12);

  // 局所の構造体初期化子
  chk('s', ct.t == (5 << 20) && ct.ref == 0);

  // ブロックの遮蔽
  i = 10;
  {
    int i;
    i = 20;
    chk('t', i == 20);
  }
  chk('u', i == 10);

  // 定数式の sizeof と列挙定数
  chk('v', sizeof(tab) == 8 && E1 == 255 && E2 == 16 && E3 == 17);

  // 8 進リテラルと大きな浮動小数点リテラル
  chk('w', 0666 == 438 && 0777 == 511);
  w.dd = 79228162514264337593543950336.0;         // 2^96
  chk('x', w.uu == 0x45F0000000000000ULL);
  w.dd = 18446744073709551616.0;                  // 2^64
  chk('y', w.uu == 0x43F0000000000000ULL);
  w.dd = 4294967296.0;                            // 2^32
  chk('z', w.uu == 0x41F0000000000000ULL);

  // ポインタ + 64 bit / 桁数が 64 bit の式
  p = (char *)1000;
  u = 24;
  p = p + u;                   chk('A', p == (char *)1024);
  big = 0x8000000000000000ULL;
  u = 3;
  s = (long long)(big >> (u & 63ULL));
  chk('B', s == 0x1000000000000000LL);

  // 狭い局所への 64 bit の初期化子
  u = 0xAABBCCDD11223344ULL;
  {
    int e = u >> 48;
    chk('C', e == 0xAABB);
  }

  // __FUNCTION__
  p = __FUNCTION__;
  chk('D', p[0] == 'm' && p[1] == 'a' && p[4] == 0);

  // 仮定義の重複と本定義
  tent = 5;
  chk('E', tent == 5 && tini == 42);

  // 関数型 typedef は「F *」でポインタになる
  {
    Fn *fp;
    fp = 0;
    chk('F', fp == 0);
  }

  // ラベルつき文は if の内側の文である
  chk('G', lab3(0, 0) == 0 && lab3(1, 0) == 1 && lab3(0, 1) == 1);

  putc(10);
  return 0;
}
