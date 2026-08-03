// 標準入力を大文字化して標準出力へ写すフィルタ (docs/stage013-tools.md 4 章)。
// シェルの `< in > out` (つなぎ替え) の検査に使う。fd 0 がファイルへ
// 結ばれている前提 (末尾で read が 0 を返す)。生のスタブだけで書く
int sys_read(int fd, char *buf, int n);
int sys_write(int fd, char *buf, int n);

int main(void) {
  char buf[64];
  int n;
  int i;
  for (;;) {
    n = sys_read(0, buf, 64);
    if (n <= 0) return 0;
    for (i = 0; i < n; i++)
      if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = buf[i] - 32;
    sys_write(1, buf, n);
  }
}
