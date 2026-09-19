/* **型名は識別子を含まない** (C89 6.5.5)。cast の型名に関数ポインタを
 * 書く形
 *
 *     (void *(*) (long)) xmalloc
 *
 * は、`cc15w` が通した仮引数並びの抽象宣言子と**同じ宣言子**だが、
 * **通る道が別**である —— 仮引数並びは `pparam`、cast は単項式の
 * `( 型 )` の枝で、`cc15w` は前者だけを直していた。`cc15x` で揃えた。
 *
 * 表に出た形は GCC 4.7.4 の `libcpp/symtab.c` 65 行 ——
 *
 *     _obstack_begin (&table->stack, 0, 0,
 *                     (void *(*) (long)) xmalloc,
 *                     (void (*) (void *)) free);
 *
 * `obstack` は確保子を `void *(*)(long)` として持つが、実際に渡すのは
 * `xmalloc` (`void *(size_t)`) なので、上流は cast で型を合わせる。
 * `symtab` / `init` / `obstack` の 3 単位がここで止まっていた
 * (docs/stage017-gcc.md 8.3 の 9)。
 *
 * **読めるだけでは足りない。** cast した関数ポインタを**実際に呼んで
 * 値を見る** —— cast が値を壊していれば呼び先が違う。 */
int putc(int c);

/* obstack と同じ形。確保子と解放子を型で受け、中で呼ぶ */
int beginlike(void *, long, void *(*)(long), void (*)(void *));

/* 渡す側の実体は仮引数の型が違う (obstack の xmalloc / free と同じ関係) */
char *alloc4(int n) { return (char *)0 + n; }
void drop(char *p) { }
char *alloc0(int n) { return (char *)0; }

int dropped;
void note(char *p) { dropped = dropped + 1; }

int beginlike(void *p, long n, void *(*al)(long), void (*fr)(void *)) {
  void *q;
  q = al(n);
  fr(p);
  return q == (void *)((char *)0 + 40);
}

int twice(int x) { return x + x; }

int f(void) {
  int (*g)(int);
  dropped = 0;

  /* 引数の位置で cast する (symtab.c 65 行と同じ形) */
  if (beginlike((void *)0, 40, (void *(*) (long)) alloc4,
		(void (*) (void *)) drop) != 1) return 'n';

  /* 確保子を替えれば答も変わること。cast が呼び先を決めている */
  if (beginlike((void *)0, 40, (void *(*) (long)) alloc0,
		(void (*) (void *)) note) != 0) return 'n';
  if (dropped != 1) return 'n';

  /* cast した値を変数へ入れてから呼ぶ道 */
  g = (int (*) (int)) twice;
  if (g(21) != 42) return 'n';

  /* cast を直に呼ぶ道 */
  if (((int (*) (int)) twice)(20) != 40) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
