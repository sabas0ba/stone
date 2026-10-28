/* ビットフィールドを，ホストの処理系と突き合わせる
 * (tools/diff17.sh。docs/stage017-gcc.md 5.2)。
 *
 * **GCC 自身がビットフィールドの塊である。** `tree` の各節は
 * `unsigned side_effects_flag : 1;` のような旗を何十も並べて持ち，
 * `gimple` も同じ形である。4 (GCC の C を我々の器へ入れる) に進んだ
 * 瞬間に，1 単位目から要る。
 *
 * 測るのは 3 つ。
 *
 *   1. **並び** —— 何バイトに収まるか (sizeof)。詰め方が違えば構造体の
 *      大きさが変わり，配列の添字がずれる
 *   2. **値の出し入れ** —— 幅で切り詰められるか。`: 3` に 9 を入れたら 1
 *   3. **符号** —— `int x : 3` は符号つきか。C89 6.5.2.1 は
 *      「`int` と書いたビットフィールドが符号つきか否かは処理系定義」と
 *      するので，**ここは合わなくてもよい**。合わないなら合わないと
 *      判ることに意味がある (黙って違うのが最悪である)
 *
 * 出すのは値だけで，並びは sizeof の数として出す。 */
int putc(int c);

static int pn(long v) {
  char b[24];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + (int)(v % 10)); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
  return 0;
}

static int nl(void) { putc('\n'); return 0; }

/* 1 語に収まる並び */
struct a {
  unsigned f1 : 1;
  unsigned f2 : 1;
  unsigned f3 : 3;
  unsigned f4 : 8;
};

/* 語をまたぐ並び。**またぐビットフィールドを割るか，次の語へ送るかは
 * 処理系が決める** (C89 6.5.2.1)。我々は送る側である */
struct b {
  unsigned x : 20;
  unsigned y : 20;
};

/* 旗と普通の欄が混ざる形 (tree の実物に近い) */
struct c {
  unsigned code : 16;
  unsigned flag : 1;
  unsigned rest : 15;
  int value;
  unsigned tail : 4;
};

/* 幅 0 は「次の境界まで送る」の意である (C89 6.5.2.1) */
struct d {
  unsigned p : 3;
  unsigned : 0;
  unsigned q : 3;
};

static struct a ga = { 1, 0, 5, 200 };
static struct c gc = { 1000, 1, 300, -7, 9 };

int main(void) {
  struct a a;
  struct b b;
  struct c c;
  struct d d;
  int i;

  /* 1. 並び */
  pn((long)sizeof(struct a));
  pn((long)sizeof(struct b));
  pn((long)sizeof(struct c));
  pn((long)sizeof(struct d));
  nl();

  /* 2. 値の出し入れ。**幅で切り詰められるか** */
  a.f1 = 1;
  a.f2 = 0;
  a.f3 = 5;
  a.f4 = 200;
  pn(a.f1); pn(a.f2); pn(a.f3); pn(a.f4);
  a.f3 = 9;                     /* 3 ビットに 9 -> 1 */
  a.f4 = 300;                   /* 8 ビットに 300 -> 44 */
  pn(a.f3); pn(a.f4);
  nl();

  /* 隣を壊さないか */
  a.f1 = 1; a.f2 = 1; a.f3 = 7; a.f4 = 255;
  a.f3 = 0;
  pn(a.f1); pn(a.f2); pn(a.f3); pn(a.f4);
  nl();

  b.x = 1000000;
  b.y = 999999;
  pn(b.x); pn(b.y);
  b.x = b.x + 1;
  pn(b.x); pn(b.y);
  nl();

  c.code = 1234;
  c.flag = 1;
  c.rest = 30000;
  c.value = -5;
  c.tail = 15;
  pn(c.code); pn(c.flag); pn(c.rest); pn((long)c.value); pn(c.tail);
  c.flag = 0;
  pn(c.code); pn(c.flag); pn(c.rest); pn((long)c.value); pn(c.tail);
  nl();

  d.p = 5;
  d.q = 6;
  pn(d.p); pn(d.q);
  nl();

  /* 3. 初期化子 */
  pn(ga.f1); pn(ga.f2); pn(ga.f3); pn(ga.f4);
  pn(gc.code); pn(gc.flag); pn(gc.rest); pn((long)gc.value); pn(gc.tail);
  nl();

  /* 4. 演算のなかで使う (格上げされて int になる) */
  a.f3 = 5;
  pn(a.f3 * 3);
  pn(a.f3 - 7);
  pn(a.f4 + 1);
  i = 0;
  a.f3 = 0;
  while (a.f3 < 7) { a.f3 = a.f3 + 1; i = i + a.f3; }
  pn(i);
  nl();

  /* 5. 配列に入れる (並びが違えば添字がずれる) */
  {
    struct a arr[4];
    for (i = 0; i < 4; i = i + 1) {
      arr[i].f1 = i & 1;
      arr[i].f2 = 0;
      arr[i].f3 = i;
      arr[i].f4 = i * 10;
    }
    for (i = 0; i < 4; i = i + 1) { pn(arr[i].f1); pn(arr[i].f3); pn(arr[i].f4); }
    nl();
  }

  /* 6. 構造体ごと写す */
  {
    struct c c2;
    c2 = c;
    pn(c2.code); pn(c2.flag); pn(c2.rest); pn((long)c2.value); pn(c2.tail);
    nl();
  }
  return 0;
}
