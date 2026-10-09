/* memprobe.c --- どれだけ記憶域が取れるかを OS の上で実測する
 *
 * 第 3 部の目的は「ヒープの上限を 14 MB から広げる」ことなので，
 * **実際に取れて，実際に書けて，実際に読み戻せる**ことを見る
 * (docs/stage016-os.md 8 章)。
 *
 * sbrk が返す値を信じるだけでは足りない。カーネルの UBRKMAX を上げた
 * だけで QEMU の RAM が足りていなければ，**確保は成功するのに書いた
 * 瞬間に落ちる**。それは台帳でいう bad なので，1 MiB ごとに印を書き，
 * 最後にもう一度なめて読み戻す。
 *
 * 出力は
 *   got <MiB>      伸ばせた量 (1 MiB 単位)
 *   verify ok|BAD  書いた印がすべて読み戻せたか
 * の 2 行だけである。上限そのものは配置で決まるので expected では
 * 「何 MiB 以上か」を見る (検査の側で判定する)。
 */
#include <stdio.h>
#include <unistd.h>

#define MB (1024 * 1024)

int main(void) {
  char *base;
  char *p;
  int i;
  int n;
  int bad;

  /* 1 MiB ずつ伸ばす。失敗した時点が上限である */
  base = (char *)sbrk(0);
  n = 0;
  while (1) {
    p = (char *)sbrk(MB);
    if (p == (char *)-1) break;
    /* 取れた 1 MiB の先頭と末尾に印を書く。ここで落ちるなら
     * 「確保できたのに使えない」という一番たちの悪い形である */
    p[0] = (char)(n & 255);
    p[MB - 1] = (char)((n ^ 0x5a) & 255);
    n = n + 1;
    if (n >= 4096) break;               /* 念のための上限 (4 GiB) */
  }

  /* 書いた印をすべて読み戻す */
  bad = 0;
  for (i = 0; i < n; i++) {
    p = base + (long)i * MB;
    if (p[0] != (char)(i & 255)) bad = 1;
    if (p[MB - 1] != (char)((i ^ 0x5a) & 255)) bad = 1;
  }

  printf("got %d\n", n);
  if (bad) printf("verify BAD\n");
  else printf("verify ok\n");
  return 0;
}
