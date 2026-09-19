/* ldin.c --- リンカへの入力を組み立てる (Stage 13 第 3 部)
 *
 * ホスト側の `{ printf 'E'; cat a.o b.o; printf '\0'; }` のゲスト版である。
 *
 *   ldin E a.o b.o > prog.ld
 *   ld < prog.ld > prog
 *
 * 先頭 1 バイトが出力形式 ('F' / 'K' / 'E')，続けてオブジェクト列，
 * 末尾に 0 を置く (stage013/ld13.md)。ld はオブジェクトの先頭バイトが
 * ELF のマジック (127) でなくなったところで読取りを止めるので，OS の
 * 上では末尾の 0 が無くても (read が 0 を返すので) 止まる。それでも
 * 置くのは，同じファイルがホスト側の経路でもそのまま通るようにする
 * ためである。
 *
 * リンカを 2 つに分けず，入力を作る側を分けたのは，ld が「標準入力を
 * 読み標準出力へ書くフィルタ」であるという既存の姿を変えないためで
 * ある。ld は凍結された世代であり，手を入れられない。
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/* 読み取りの控え。**大域に置く。** cc の 1 関数のフレーム総サイズは
 * 2040 バイトまでである (docs/dev-notes.md 4 章) */
char b[4096];

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
  char f;

  if (argc < 3) {
    fputs("usage: ldin {F|K|E} obj...\n", stderr);
    return 2;
  }
  f = argv[1][0];
  if (f != 'F' && f != 'K' && f != 'E') {
    fputs("ldin: bad format\n", stderr);
    return 2;
  }
  putchar(f);
  for (i = 2; i < argc; i++) {
    if (emit(argv[i]) < 0) {
      fputs("ldin: cannot read ", stderr);
      fputs(argv[i], stderr);
      fputs("\n", stderr);
      return 1;
    }
  }
  putchar(0);
  return 0;
}
