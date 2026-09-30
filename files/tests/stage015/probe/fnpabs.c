/* **プロトタイプの仮引数は名前を省ける** (C89 6.5.4 の抽象宣言子)。
 * 関数ポインタもその例外ではなく
 *
 *     extern int g(int (*)(int));
 *
 * は妥当な宣言である。`cc15v` までは仮引数の関数ポインタの宣言子を
 * 「( * 名前 ) ( 仮引数 )」の形でしか読まず，名前の位置に ')' が来ると
 * 1 (構文誤り) で止まっていた。**名前を書けば通る**ので，欠けていたのは
 * 抽象宣言子の側だけである。
 *
 * 表に出た形は GCC 4.7.4 の include/obstack.h 193 行 ——
 *
 *     extern int _obstack_begin (struct obstack *, int, int,
 *                                void *(*) (long), void (*) (void *));
 *
 * で、libcpp の全単位が include/symtab.h 経由でこれを読む。12 単位が
 * ここで止まっていた (docs/stage017-gcc.md 8.3 の 4)。
 *
 * 読めるだけでは足りないので、**同じ関数を抽象宣言子のプロトタイプで
 * 宣言し、名前つきの定義で定義し、実際に呼んで値を見る**。仮引数の
 * 位置がずれていれば値が違う。 */
int putc(int c);

/* ---- プロトタイプはすべて抽象宣言子で書く ---- */
int apply(int (*)(int), int);
int after(int, int (*)(int));
int between(int, int (*)(int), int);
int two(int (*)(int), int (*)(int, int), int, int);

/* 返却型がポインタの形 (obstack.h の void *(*) (long) と同じ並び) */
char *pick(char *(*)(char *), char *);

/* obstack.h 193 行そのものの並び —— スカラと関数ポインタが混ざる。
 * **宣言だけにはしない。** 我々の cc は宣言した関数を記号表へ載せ，
 * ld は定義を要求するので、定義して実際に呼ぶ */
int beginlike(void *, int, int, void *(*)(int), void (*)(void *));

/* ---- 定義は名前つきで書く ---- */
int dbl(int x) { return x + x; }
int inc(int x) { return x + 1; }
int add(int a, int b) { return a + b; }
char *same(char *p) { return p; }
void *nullof(int n) { return (void *)0; }
void freeit(void *p) { }

int beginlike(void *p, int a, int b, void *(*al)(int), void (*fr)(void *)) {
  if (al(a) != (void *)0) return 0;
  fr(p);
  return a + b;
}

int apply(int (*f)(int), int x) { return f(x); }
int after(int x, int (*f)(int)) { return f(x); }
int between(int a, int (*f)(int), int b) { return f(a) + b; }
int two(int (*f)(int), int (*g)(int, int), int a, int b) {
  return g(f(a), b);
}
char *pick(char *(*f)(char *), char *p) { return f(p); }

int f(void) {
  char s[4];

  /* 先頭・末尾・中間のどこに置いても仮引数の位置が合うこと */
  if (apply(dbl, 21) != 42) return 'n';
  if (after(20, inc) != 21) return 'n';
  if (between(20, dbl, 2) != 42) return 'n';
  if (two(dbl, add, 20, 2) != 42) return 'n';

  s[0] = 'o'; s[1] = 'k'; s[2] = 0;
  if (pick(same, s) != s) return 'n';

  /* obstack.h の並びそのもの。スカラ 2 つを挟んで関数ポインタが 2 つ */
  if (beginlike(s, 40, 2, nullof, freeit) != 42) return 'n';

  /* 名前つきの宣言子は今までどおり通ること */
  if (apply(inc, 41) != 42) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
