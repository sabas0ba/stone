// 仕様 2.6 節: getc/putc によるフィルタ。'.' まで転写し英小文字を大文字化する。
int main() {
  int c;
  c = getc();
  while (c != '.') {
    if (c >= 'a') {
      if (c <= 'z') c = c - 32;
    }
    putc(c);
    c = getc();
  }
  return 0;
}
