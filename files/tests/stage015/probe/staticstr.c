/* 静的な器の初期化子に**文字列リテラル**が出てくるとき。
 *
 * cc15p まではここが 2 通りに壊れていた。どちらも **通ってしまうが
 * 値が違う** —— 台帳で bad と呼ぶ状態である。cc15q で直した
 * (docs/stage017-cc.md 24〜25 章 / 27〜28 章)。
 *
 *   その 1  関数内 static がポインタで受ける形。予約したローカル
 *           シンボルの位置 (lsoff) を誰も埋めないので，ポインタが
 *           別の場所を指す。tcc の tccgen.c の parse_atomic() が
 *             static const char *const templates[] = { "alm.?", ... };
 *           で使う。ここが壊れると引数の個数を駆動する文字が読めず，
 *           正しい C を誤りとして拒む。
 *
 *   その 2  構造体の中の char 配列で受ける形。ginit1() が対象の型を
 *           見ずにポインタとして扱うので，**配列の中身がアドレスに
 *           なる**。tcc の tcctools.c の arhdr_init がこの形で，
 *           ここが壊れると tcc -ar が壊れた書庫を吐く。
 *
 * Stage 14 の台帳には cc14g で測った bad が載っている。こちらは
 * 最前線の世代で測るので ok である。
 *
 * **まだ直っていない形は別の probe (staticstr2.c) に置いてある。**
 * ここに混ぜると，1 つの行に「直った」と「壊れている」が同居して
 * 台帳の「通るが誤り」の数え上げが 0 になってしまう。 */
int putc(int c);

typedef struct { char a[8]; } S1;
typedef struct { char a[8]; char b[4]; } S2;
typedef struct { int n; char a[8]; } S3;

static struct { char nm[16]; char sz[10]; } gh = { "/", "2636" };

int f(void) {
  /* その 1 */
  static char *p = "A";
  static char *t[2] = { "B", "C" };
  /* その 2 (関数内 static) */
  static S1 s1 = { "abc" };
  static S2 s2 = { "abc", "xy" };
  static S3 s3 = { 7, "abc" };
  /* **ポインタの配列**。ここは実体ではなく再配置を置く場所である。
   * 「配列なら並べる」と読むとポインタの枠に字が入り，参照先が不正に
   * なって落ちる (レビューで指摘を受けて直した。cc15q は文字型の配列
   * だけを受ける) */
  static struct { char *p[1]; int n; } sp = { "A", 5 };
  /* 前からできていた形。直しで壊していないことを見る */
  static char c[8] = "abc";
  static int  n[2] = { 3, 4 };
  if (p[0] != 'A') return 'n';
  if (t[0][0] != 'B' || t[1][0] != 'C') return 'n';
  if (s1.a[0] != 'a' || s1.a[3] != 0) return 'n';
  if (s2.a[0] != 'a' || s2.b[0] != 'x') return 'n';
  if (s3.n != 7 || s3.a[0] != 'a') return 'n';
  if (sp.p[0][0] != 'A' || sp.n != 5) return 'n';
  if (c[0] != 'a' || n[0] != 3) return 'n';
  /* その 2 (大域の static。関数内かどうかに関係なく同じ道) */
  if (gh.nm[0] != '/' || gh.sz[0] != '2') return 'n';
  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
