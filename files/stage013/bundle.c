/* bundle.c --- ソース群を pp への入力 (束ね) へ変換する (Stage 13 第 3 部)
 *
 * ホスト側の tools/bundle.sh のゲスト版である。形式は
 * docs/stage009-pp.md 2.2。
 *
 *   bundle := "#!stone-bundle\n" member* EOT
 *   member := "@" name " " size "\n" content
 *
 * 最後に並べたファイルが前処理対象の翻訳単位になる。それより前のものは
 * #include で参照できる (依存を先に，本体を後に並べる)。
 *
 *   bundle util.h main.c > main.b
 *
 * **末尾の EOT が要である。** OS の上では入力の終わりで read が 0 を
 * 返すが，pp と cc は EOT でしか読取りを止めない (どちらも凍結された
 * 世代なので手は入れられない)。EOT を置くのは束ねを作るこの側の役目で
 * ある (docs/stage013-tools.md 7.2)。
 *
 * 名前は与えられた引数をそのまま使う。sfs の名前空間はフラットなので
 * ホスト版の basename にあたる処理は要らない (docs/stage012-os.md 4.2)。
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* 読み取りの控え。**大域に置く。** cc の 1 関数のフレーム総サイズは
 * 2040 バイトまでで，これを超えると領域超過 (終了コード 6) になる
 * (docs/dev-notes.md 4 章)。大きな作業領域は局所に取らない */
char b[4096];

/* ファイルの大きさを数える。読めなければ -1。
 * 大きさを本文より先に出す形式なので，開いて 2 度読む。全体を溜める
 * 記憶域を要らなくするための割り切りである */
int fsize(char *name) {
  int fd;
  int n;
  int t;

  fd = open(name, O_RDONLY);
  if (fd < 0) return -1;
  t = 0;
  for (;;) {
    n = read(fd, b, 4096);
    if (n <= 0) break;
    t = t + n;
  }
  close(fd);
  if (n < 0) return -1;
  return t;
}

/* ファイルの中身を標準出力へ写す。誤りなら -1 */
int emit(char *name) {
  int fd;
  int n;

  fd = open(name, O_RDONLY);
  if (fd < 0) return -1;
  for (;;) {
    n = read(fd, b, 4096);
    if (n <= 0) break;
    if (write(1, b, n) != n) { close(fd); return -1; }
  }
  close(fd);
  if (n < 0) return -1;
  return 0;
}

int main(int argc, char **argv) {
  int i;
  int n;

  if (argc < 2) {
    fputs("usage: bundle file...\n", stderr);
    return 2;
  }
  fputs("#!stone-bundle\n", stdout);
  for (i = 1; i < argc; i++) {
    n = fsize(argv[i]);
    if (n < 0) {
      fputs("bundle: cannot read ", stderr);
      fputs(argv[i], stderr);
      fputs("\n", stderr);
      return 1;
    }
    printf("@%s %d\n", argv[i], n);
    if (emit(argv[i]) < 0) return 1;
  }
  putchar(4);                           /* EOT */
  return 0;
}
