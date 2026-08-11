// 標準入力をそのまま標準出力へ写すフィルタ。kernel14 の sfs の上書き検査で
// 使う (docs/stage014-external.md 13.1)。シェルの `< in > out` で結ぶので，
// 出力先が「既に在るファイル」か「新規」かを検査側が選べる。
// 生のスタブだけで書く (libc を並べない)
int sys_read(int fd, char *buf, int n);
int sys_write(int fd, char *buf, int n);

int main(void) {
  char buf[64];
  int n;
  int w;
  int k;
  for (;;) {
    n = sys_read(0, buf, 64);
    if (n <= 0) return 0;
    k = 0;
    while (k < n) {
      w = sys_write(1, buf + k, n - k);
      if (w <= 0) return 1;             // 領域不足 (-ENOSPC) なら止める
      k = k + w;
    }
  }
}
