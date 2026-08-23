/* stamp.c --- ファイルの長さ・更新時刻・種別を出す (第 4 部の 1 の検査)
 *
 *   stamp 経路...
 *
 * 1 行 1 ファイルで
 *
 *   <種別> <長さ> <上位語> <下位語> <経路>
 *
 * を出す。時刻は **epoch からのナノ秒を u32 2 本のまま**出す ——
 * 秒に直すと 64 bit の除算が要るうえ，ホスト側と突き合わせるときに
 * 丸めが入る (docs/stage017-cc.md 11.2)。
 *
 * 検査はこの出力を，ホストが同じファイルに与えた mtime と突き合わせる。
 * **一致すれば，カーネルが読んだ時刻が本物である**ことが判る。
 */
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
  int i;
  struct stat st;
  int bad;
  bad = 0;
  for (i = 1; i < argc; i = i + 1) {
    if (stat(argv[i], &st) < 0) {
      printf("? %s\n", argv[i]);
      bad = 1;
      continue;
    }
    printf("%c %ld %u %u %s\n",
           st.st_type == S_TYPE_DIR ? 'd' : 'f',
           st.st_size, st.st_mthi, st.st_mtlo, argv[i]);
  }
  return bad;
}
