// argv を 1 行ずつ出し，argc を終了コードにする (docs/stage013-tools.md 4 章)。
// boot 行の引数分割と，spawn / シェル経由の argv の両方をこれで検査する。
// 前置部の putc だけで書き，libc はリンクしない (pp も通さないので // で書く)
int putc(int c);

int main(int argc, char **argv) {
  int i;
  int j;
  char *p;
  for (i = 0; i < argc; i++) {
    p = argv[i];
    for (j = 0; p[j]; j++) putc(p[j]);
    putc('\n');
  }
  return argc;
}
