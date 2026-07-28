// 第 3 部の 3: 構造体を値として扱う (docs/stage010-c89.md 17 章)
//
// 第 3 部の 2 までは，構造体の代入と値渡しが「先頭 4 バイトだけ複写する」
// 形で静かに壊れていた。ここで見たいのは値が合うことそのものより，
// **実体の全体が運ばれていること** である。そこで
//   - 4 バイトを超える構造体だけを使う
//   - 先頭以外のメンバの値を必ず出力に混ぜる
//   - 呼ばれた側で書き換えても呼んだ側に響かないこと (値渡しであること)
// を確かめる。

struct P { int x; int y; int z; };
struct Inner { int a; int b; };
struct Outer { int tag; struct Inner in; char name[4]; };

struct P gx;
struct P gy;
struct Outer go;

char ob[16];
int pn(int v) {
  int n;
  n = 0;
  if (v == 0) { ob[0] = '0'; n = 1; }
  while (v > 0) { ob[n] = '0' + v % 10; n = n + 1; v = v / 10; }
  while (n > 0) { n = n - 1; putc(ob[n]); }
  putc(' ');
  return 0;
}

int enc(struct P p) { return p.x * 100 + p.y * 10 + p.z; }

// 呼ばれた側で書き換える。値渡しなので呼んだ側の実体は変わらない
int clobber(struct P p) { p.x = 0; p.y = 0; p.z = 0; return enc(p); }

// 構造体を 2 個。両方が独立に届くこと
int two(struct P a, struct P b) { return enc(a) * 1000 + enc(b); }

// スカラと混在。前後の引数が構造体に押しつぶされないこと
int mix(int k, struct P p, int m) { return k * 10000 + enc(p) * 10 + m; }

// 入れ子のメンバをそのまま値渡しする
int inner(struct Inner i) { return i.a * 10 + i.b; }

int main() {
  struct P lp;
  struct P lq;
  struct P lt[3];
  struct Outer lo;
  int i;

  gx.x = 1; gx.y = 2; gx.z = 3;
  gy.x = 9; gy.y = 9; gy.z = 9;
  gy = gx;                      // 大域どうしの代入
  pn(enc(gy));

  lp.x = 4; lp.y = 5; lp.z = 6;
  lq = lp;                      // 局所どうしの代入
  pn(enc(lq));
  lq = gx;                      // 大域から局所へ
  pn(enc(lq));

  pn(enc(lp));                  // 値渡し
  pn(clobber(lp)); pn(enc(lp)); // 呼ばれた側の書換えが響かないこと
  pn(two(lp, gx));
  pn(mix(7, lp, 8));

  // 局所の構造体配列
  for (i = 0; i < 3; i++) { lt[i].x = i; lt[i].y = i * 2; lt[i].z = i * 3; }
  for (i = 0; i < 3; i++) pn(enc(lt[i]));
  lt[2] = lt[0];
  pn(enc(lt[2]));

  // 入れ子
  go.tag = 2; go.in.a = 3; go.in.b = 4;
  go.name[0] = 'h'; go.name[1] = 'i'; go.name[2] = 0;
  lo = go;                      // 入れ子を含む構造体まるごとの代入
  pn(lo.tag * 100 + lo.in.a * 10 + lo.in.b);
  putc(lo.name[0]); putc(lo.name[1]); putc(' ');
  pn(inner(lo.in));             // メンバの構造体を値渡し

  pn(sizeof(struct P));
  pn(sizeof(struct Outer));
  pn(sizeof(lo.in));
  pn(sizeof(lt));
  putc(10);
  return 0;
}
