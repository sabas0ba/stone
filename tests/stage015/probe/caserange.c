/* 範囲つき case (case lo ... hi:。GNU C。cc15aa。docs/stage018-ext.md 11)。
 *
 * カーネルは文字の分類や状態機械でこの形を使う。書き換えれば避けられる
 * 形ではあるが、**書き換えると 256 行になる**ことがある。
 *
 * 期待出力: abcdefg */
int chk(int ch, int ok) { if (!ok) putc('X'); putc(ch); return 0; }

int classify(int c) {
  switch (c) {
  case '0' ... '9': return 1;
  case 'a' ... 'z': return 2;
  case 'A' ... 'Z': return 3;
  case ' ':
  case '\t': return 4;
  default: return 0;
  }
}

int edge(int v) {
  switch (v) {
  case 10 ... 10: return 1;      /* 1 点の範囲 */
  case 20 ... 19: return 2;      /* 空の範囲。C は「合致しない」と定める */
  case 30 ... 40: return 3;
  }
  return 0;
}

int main() {
  chk('a', classify('5') == 1);
  chk('b', classify('q') == 2);
  chk('c', classify('Q') == 3);
  chk('d', classify('\t') == 4);
  chk('e', classify('+') == 0);
  chk('f', edge(10) == 1 && edge(19) == 0 && edge(20) == 0);
  chk('g', edge(30) == 3 && edge(40) == 3 && edge(41) == 0);
  putc(10);
  return 0;
}
