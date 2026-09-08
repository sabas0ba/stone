/* 指示つき初期化子 (C99 6.7.8。cc15x。docs/stage018-ext.md 3.5)。
 *
 *   構造体   .name = v
 *   配列     [k] = v
 *
 * 見るのは値だけではなく**触れなかった場所が 0 になること**である。
 * 指示子は書く位置を跳ばすので，跳んだ手前が埋まらないまま残ると
 * 「動くが違う」になる。前へ跳ぶ形と後ろへ戻る形の両方を置く。
 *
 * GCC 4.7 の C も Linux も当たり前に使う形で，**我々のソースは 1 つも
 * 使っていない**。
 *
 * 期待出力: abcdefghij */
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }

struct S { int a; int b; int c; };

/* 大域: 順を入れ替える / 隙間を空ける / 混ぜる */
static struct S g1 = { .b = 2, .a = 1, .c = 3 };
static struct S g2 = { .c = 9 };
static struct S g3 = { 1, .c = 7 };
static int ga[5] = { [4] = 5, [1] = 2 };
static int gb[5] = { 1, [3] = 4, 5 };
static char gc[6] = { [5] = 'z', [0] = 'a' };

int main() {
  chk('a', g1.a == 1 && g1.b == 2 && g1.c == 3);
  chk('b', g2.a == 0 && g2.b == 0 && g2.c == 9);
  /* 指示子の前は宣言順に埋まり，指示子から先はその位置へ移る */
  chk('c', g3.a == 1 && g3.b == 0 && g3.c == 7);
  chk('d', ga[0] == 0 && ga[1] == 2 && ga[2] == 0 && ga[3] == 0 && ga[4] == 5);
  /* [3] へ跳んだ後の 5 は [4] に入る */
  chk('e', gb[0] == 1 && gb[1] == 0 && gb[2] == 0 && gb[3] == 4 && gb[4] == 5);
  chk('f', gc[0] == 'a' && gc[1] == 0 && gc[4] == 0 && gc[5] == 'z');

  {
    struct S s;
    int la[5];
    int lb[4];
    s.a = 5; s.b = 5; s.c = 5;
    {
      struct S t = { .b = 7 };
      chk('g', t.a == 0 && t.b == 7 && t.c == 0);
    }
    {
      struct S u = { .c = 1, .a = 2 };
      chk('h', u.a == 2 && u.b == 0 && u.c == 1);
    }
    la[0] = 9; la[1] = 9; la[2] = 9; la[3] = 9; la[4] = 9;
    {
      int v[5] = { [2] = 3 };
      chk('i', v[0] == 0 && v[1] == 0 && v[2] == 3 && v[3] == 0 && v[4] == 0);
    }
    {
      int v[4] = { 9, [2] = 8, 7 };
      chk('j', v[0] == 9 && v[1] == 0 && v[2] == 8 && v[3] == 7);
    }
    lb[0] = la[0] + s.a;
    if (lb[0] == 0) putc('X');
  }
  putc(10);
  return 0;
}
