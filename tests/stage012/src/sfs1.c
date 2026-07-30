// sfs の読み書き (docs/stage012-os.md 4 章 / 8 章の第 1 部)
//
// ベアメタルで共有領域 (0x8400_0000) の sfs を直接操作する。
//   1. マジックを確認して 'M' を出力する
//   2. dir/in.txt を探して内容を UART へ出力する
//   3. out.txt を空き項目へ作成し，データを書き，カーソルを進める
// ホスト側 (tests/stage012/test.sh) が書き戻されたイメージを回収して
// out.txt の内容と既存ファイルの保存を照合する。
//
// pp を通さずコンパイルするので // コメントで書く (docs/dev-notes.md 4 章)。

int putc(int c);

unsigned rd(char *base, unsigned off) { return *(unsigned *)(base + off); }
int wr(char *base, unsigned off, unsigned v) {
  *(unsigned *)(base + off) = v;
  return 0;
}

int streq(char *a, char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

int main() {
  char *base;
  char *msg;
  char *name;
  unsigned tbl;
  unsigned cnt;
  unsigned cur;
  unsigned i;
  unsigned j;
  unsigned eo;
  unsigned off;
  unsigned len;

  base = (char *)0x84000000;

  if (!(base[0] == 's' && base[1] == 'f' && base[2] == 's'
        && base[3] == '1')) {
    putc('X');
    putc('\n');
    return 1;
  }
  putc('M');
  putc(' ');

  tbl = rd(base, 8);
  cnt = rd(base, 12);
  cur = rd(base, 16);

  // dir/in.txt の内容を出力する
  for (i = 0; i < cnt; i++) {
    eo = tbl + i * 64;
    if (rd(base, eo + 60) == 1 && streq(base + eo, "dir/in.txt")) {
      off = rd(base, eo + 52);
      len = rd(base, eo + 56);
      for (j = 0; j < len; j++) putc(base[off + j]);
    }
  }

  // out.txt を空き項目へ作成する (未使用の項目は全 0 なので名前の
  // NUL 終端は書かなくてよい)
  msg = "written-by-guest\n";
  name = "out.txt";
  for (i = 0; i < cnt; i++) {
    eo = tbl + i * 64;
    if (rd(base, eo + 60) == 0) {
      len = 0;
      while (msg[len]) { base[cur + len] = msg[len]; len++; }
      for (j = 0; name[j]; j++) base[eo + j] = name[j];
      wr(base, eo + 52, cur);
      wr(base, eo + 56, len);
      wr(base, eo + 60, 1);
      wr(base, 16, (cur + len + 3) / 4 * 4);
      putc('W');
      putc('\n');
      return 0;
    }
  }
  putc('F');
  putc('\n');
  return 2;
}
