/* 第 3 部の 2: 可変長引数 (docs/stage010-c89.md 16 章)
 *
 * 検査したいのは値そのものより「積んだものが正しく取り出せること」で
 * ある。とくに
 *   - 名前つき引数と可変部が混ざらないこと
 *   - 可変部の個数が呼出しごとに変わっても壊れないこと
 *   - 可変長の呼出しの後でデータスタックが元に戻ること
 *     (戻らなければ繰り返し呼ぶうちに底を突く)
 * を見る。最後の 1 つは，同じ呼出しを何度も繰り返して確かめる。
 */
#include <stdarg.h>

int putc(int c);

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

/* 個数を先に受け取る形 */
int sum(int n, ...) {
  va_list ap;
  int t;
  int i;
  va_start(ap, n);
  t = 0;
  for (i = 0; i < n; i++) t += va_arg(ap, int);
  va_end(ap);
  return t;
}

/* 名前つきが 2 個ある形。名前つきと可変部が混ざらないことを見る */
int span(int lo, int hi, ...) {
  va_list ap;
  int t;
  int i;
  va_start(ap, hi);
  t = lo * 100 + hi * 10;
  for (i = 0; i < hi - lo; i++) t += va_arg(ap, int);
  va_end(ap);
  return t;
}

/* 書式で個数と型が決まる形 (printf の骨格) */
int prf(char *f, ...) {
  va_list ap;
  char *s;
  int n;
  va_start(ap, f);
  n = 0;
  while (*f) {
    if (*f == '%') {
      f++;
      if (*f == 'd') { pn(va_arg(ap, int)); n++; }
      else if (*f == 'c') { putc(va_arg(ap, int)); n++; }
      else if (*f == 's') { s = va_arg(ap, char *); while (*s) { putc(*s); s++; } n++; }
      else putc(*f);
      f++;
    } else {
      putc(*f);
      f++;
    }
  }
  return n;
}

/* 可変長を呼ぶ関数を，さらに可変長から呼ぶ (入れ子) */
int nest(int k, ...) {
  va_list ap;
  int a;
  int b;
  va_start(ap, k);
  a = va_arg(ap, int);
  b = va_arg(ap, int);
  va_end(ap);
  return sum(3, k, a, b);
}

int main() {
  int i;
  pn(sum(0));
  pn(sum(1, 5));
  pn(sum(4, 1, 2, 3, 4));
  pn(span(1, 3, 10, 20));
  pn(nest(7, 8, 9));
  pn(prf("[%d %c %s]\n", 42, 'x', "str"));
  /* 繰り返してもデータスタックが減らないこと */
  for (i = 0; i < 200; i++) sum(4, 1, 2, 3, 4);
  pn(sum(2, 100, 23));
  putc('\n');
  return 0;
}
