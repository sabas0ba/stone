/* **`typedef` は宣言子を書く** (C89 3.5.6)。記憶域クラス指定子が
 * `typedef` であるだけで、宣言子の形は何でもよい。したがって
 *
 *     typedef char t[4];
 *
 * は妥当で、`t` は `char[4]` の別名になる。`cc15aa` までは
 * `typedef1()` が**宣言子の後置 `[n]` を読んでいなかった**ので、名前の
 * 次が `[` だと `;` の検査に落ちて 1 (構文誤り) になった。
 *
 * `char a[4];` も `t a;` も前から通っていた —— **`typedef` の側だけ**が
 * 後置を読む処理を持っていなかった。`cc15ab` で `pdims()` を呼んで
 * 揃えた。
 *
 * 表に出た形は GCC 4.7.4 の `libcpp/lex.c` 133 行 ——
 *
 *     typedef unsigned long word_type;
 *     typedef char check_word_type_size
 *       [(sizeof(word_type) == 8 || sizeof(word_type) == 4) * 2 - 1];
 *
 * 大きさが 0 以下なら翻訳が落ちる、という翻訳時の表明である。
 * `libcpp/lex` 1 単位が止まっていた (docs/stage017-gcc.md 8.3 の 10)。
 *
 * **大きさの定数式は前から通っていた。** `extern char a[sizeof(int)];`
 * は `cc15aa` でも通る。落ちていたのは `typedef` の宣言子だけである。 */
int putc(int c);

typedef char t4[4];
typedef int m23[2][3];
typedef unsigned long word_type;

/* lex.c 133 行と同じ形。大きさが 1 になる (RV32 では long は 4 バイト) */
typedef char check_word_type_size
  [(sizeof(word_type) == 8 || sizeof(word_type) == 4) * 2 - 1];

/* 関数型の typedef を壊していないこと (名前の次が '(' の道) */
typedef int F(int);

int dbl(int x) { return x + x; }

/* typedef した配列を仮引数に書く。C の規則で先頭要素へのポインタになる */
int first(t4 a) { return a[0]; }

int f(void) {
  t4 a;
  m23 g;
  check_word_type_size ck;
  F *fp;
  int i; int j;

  a[0] = 'a'; a[1] = 'b'; a[2] = 'c'; a[3] = 0;
  if (a[0] != 'a' || a[3] != 0) return 'n';
  if (sizeof(t4) != 4) return 'n';
  if (first(a) != 'a') return 'n';

  /* 多次元。畳んだ配置が ふつうの宣言と同じであること */
  i = 0;
  while (i < 2) {
    j = 0;
    while (j < 3) { g[i][j] = i * 10 + j; j = j + 1; }
    i = i + 1;
  }
  if (g[0][0] != 0 || g[1][2] != 12) return 'n';
  if (sizeof(m23) != 24) return 'n';

  /* 翻訳時の表明。RV32 では long が 4 バイトなので大きさは 1 */
  ck[0] = 1;
  if (sizeof(check_word_type_size) != 1) return 'n';
  if (ck[0] != 1) return 'n';

  fp = dbl;
  if (fp(21) != 42) return 'n';

  return 'y';
}

int main() { putc(f()); putc('\n'); return 0; }
