int g[4];
char pb[16];
int p(int v) {
  int n;
  if (v < 0) { putc('-'); v = -v; }
  n = 0;
  if (v == 0) { pb[0] = '0'; n = 1; }
  while (v > 0) { pb[n] = '0' + v % 10; n = n + 1; v = v / 10; }
  while (n > 0) { n = n - 1; putc(pb[n]); }
  putc(' ');
  return 0;
}
int main() {
  int i; int s; int j; char b[10]; char *q; int k; int *r;
  // for
  s = 0;
  for (i = 0; i < 5; i = i + 1) s = s + i;
  p(s);                       // 10
  // for + ++ + 複合代入
  s = 0;
  for (i = 0; i < 5; i++) s += i * 2;
  p(s);                       // 20
  // 歩進なしの for + continue + break
  s = 0; i = 0;
  for (; i < 10;) { i++; if (i == 3) continue; if (i == 6) break; s += i; }
  p(s);                       // 1+2+4+5 = 12
  // do-while
  s = 0; i = 0;
  do { s += i; i++; } while (i < 4);
  p(s);                       // 6
  // switch: フォールスルーと default
  s = 0;
  for (i = 0; i < 5; i++) {
    switch (i) {
    case 0: s += 1;
    case 1: s += 10; break;
    case 3: s += 100; break;
    default: s += 1000;
    }
  }
  p(s);                       // 1+10 +10 +1000 +100 +1000 = 2121
  // switch の入れ子と，switch 内の continue が外側ループへ効くこと
  s = 0;
  for (i = 0; i < 3; i++) {
    switch (i) {
    case 1: continue;
    default:
      switch (i) {
      case 0: s += 1; break;
      default: s += 2;
      }
    }
    s += 100;
  }
  p(s);                       // (1+100) + (2+100) = 203
  // ?:
  i = 3;
  p(i > 2 ? 7 : 8);           // 7
  p(i > 9 ? 7 : 8);           // 8
  // 前置・後置 ++/--
  i = 5;
  p(i++); p(i); p(++i); p(i--); p(--i);   // 5 6 7 7 5
  // 複合代入いろいろ
  i = 12; i -= 2; i *= 3; i /= 5; i %= 4; p(i);   // 12-2=10*3=30/5=6%4=2
  i = 6; i &= 3; p(i); i |= 8; p(i); i ^= 1; p(i);  // 2 10 11
  i = 1; i <<= 4; p(i); i >>= 2; p(i);            // 16 4
  // ポインタの ++ と +=
  g[0] = 1; g[1] = 2; g[2] = 3; g[3] = 4;
  r = g; r++; p(*r); r += 2; p(*r); r--; p(*r);
  // カンマ演算子
  i = (j = 2, j + 3);
  p(i);                       // 5
  // sizeof
  p(sizeof(int)); p(sizeof(char)); p(sizeof(char *));   // 4 1 4
  p(sizeof(b)); p(sizeof(g)); p(sizeof b);              // 10 16 10
  p(sizeof(i));                                          // 4
  // キャスト
  q = (char *)g;
  p((int)q == (int)g);        // 1
  // goto
  k = 0;
  i = 0;
again:
  i++;
  if (i < 4) goto again;
  p(i);                       // 4
  goto done;
  k = 99;
done:
  p(k);                       // 0
  putc(10);
  return 0;
}
