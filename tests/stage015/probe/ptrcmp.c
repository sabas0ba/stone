/* ポインタの大小比較を，ホストの処理系と突き合わせる (tools/diff17.sh)。
 * docs/stage017-gcc.md 8.15 / stage015/cc15ar.sc。
 *
 * ポインタの大小は番地の符号なしの大小である。我々の RAM は 0x8000_0000 から
 * 上にあるので，符号つきで比べると番地はすべて負になり，NULL より小さく
 * 見える。GCC の default_elf_asm_output_ascii は `s > last_null` (last_null
 * の初期値は NULL) で分岐し，cc1 の .s で `.string ""` が `.ascii "\000"` に
 * なった。
 *
 * 見るもの: NULL との < <= > >=，配列の中の 2 つの番地，(char *) 0 と
 * 大きな番地を整数から作った値，?: と if と while の中の比較。 */
int putc(int c);

static void pn(int v) {
  putc('0' + v);
  putc(' ');
}

static char buf[16];
static char *last;

int main(void) {
  char *s;
  char *z;
  char *hi;
  int n;
  s = buf + 3;
  z = (char *) 0;
  hi = (char *) 0;
  hi = hi + 0x7fffffffL;
  hi = hi + 0x10;                     /* 0x8000000f: 符号つきなら負 */
  pn(s > z); pn(s >= z); pn(s < z); pn(s <= z);
  pn(z < s); pn(z > s);
  pn(hi > z); pn(hi < z); pn(hi > (char *) 0x7fffffffL);
  pn(buf + 1 < buf + 2); pn(buf + 5 > buf + 2);
  last = 0;
  pn(s > last ? 1 : 0);
  n = 0;
  if (s > last) n = n + 1;
  while (last < s && n < 5) n = n + 2;
  pn(n);
  putc('\n');
  return 0;
}
