/* pathprobe.c --- kernel17 の経路解決を OS の上から確かめる
 *
 * sfs2 は名前を「そのディレクトリ内での名前」として持ち，経路は親を
 * 辿って組み立てる (docs/stage016-os.md 6.3)。sfs1 は経路まるごとを
 * 1 個の名前にしていたので，同名別ディレクトリが作れなかった。
 *
 * ここで見るのは 6 点である。
 *   1. 絶対経路が引ける                (/top.txt)
 *   2. 先頭の / が無くても引ける        (top.txt)
 *   3. 深い経路が引ける                (/src/a/b/three.c)
 *   4. 同名別ディレクトリが別物である    (/inc/one.c と /src/one.c)
 *   5. 無い経路は失敗する              (/nosuch.txt, /src/nosuch.c)
 *   6. ファイルを途中に挟む経路は失敗する (/top.txt/x)
 *
 * 出力は 1 行 1 件の「名札 期待 実測」で，突き合わせは expected/ が持つ。
 * 期待もこちらが書き出すのは，読んだ人が diff の 1 行だけで
 * 「何を測ってどうずれたか」を分かるようにするためである。
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

char buf[128];

/* 経路を開いて中身 (末尾の改行を落としたもの) を buf へ置く。
 * 開けなければ 0，開けたら 1 を返す */
int slurp(char *path) {
  int fd;
  int r;
  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  r = read(fd, buf, 127);
  close(fd);
  if (r < 0) r = 0;
  while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) r = r - 1;
  buf[r] = 0;
  return 1;
}

/* expect が 0 なら「開けないこと」を期待する */
void want(char *label, char *path, char *expect) {
  int ok;
  ok = slurp(path);
  if (expect == 0) expect = "enoent";
  if (!ok) {
    printf("%s %s enoent\n", label, expect);
    return;
  }
  printf("%s %s %s\n", label, expect, buf);
}

int main(void) {
  want("abs", "/top.txt", "TOP");
  want("rel", "top.txt", "TOP");
  want("deep", "/src/a/b/three.c", "THREE");
  want("dup-inc", "/inc/one.c", "INC-ONE");
  want("dup-src", "/src/one.c", "SRC-ONE");
  want("slashes", "//src///one.c", "SRC-ONE");
  want("miss-top", "/nosuch.txt", 0);
  want("miss-deep", "/src/nosuch.c", 0);
  want("thru-file", "/top.txt/x", 0);
  want("miss-dir", "/nodir/one.c", 0);
  printf("done\n");
  return 0;
}
