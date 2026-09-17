/* stat の欄を，ホストの処理系と突き合わせる
 * (tools/diff17.sh の OS 側。docs/stage017-gcc.md 8.5)。
 *
 * 第 24 世代で `struct stat` に `st_dev` / `st_ino` / `st_mode` /
 * `st_mtime` を足した。GCC 4.7.4 の 5 単位がこの 4 つで止まっていた。
 *
 * ---- 何を突き合わせるか ----
 *
 * **値そのものは突き合わせられない。** 項目番号も装置番号も時刻も，
 * ホストと stone の OS で同じになる理由が無い。突き合わせるのは
 * **欄が満たすべき性質**である ——
 *
 *   同じファイルを 2 度見れば同じ番号になるか
 *   違うファイルなら違う番号になるか
 *   1 つの巻の上では装置番号が揃うか
 *   種別のビットが S_ISREG / S_ISDIR と噛み合うか
 *   時刻が本当の暦の値か (紀元の 0 ではないか)
 *
 * これらはホストでも我々でも同じ答になるはずで，**違えば片方が嘘を
 * ついている**。埋め草を置いた欄は必ずここで落ちる —— 例えば st_ino を
 * 0 で埋めれば「違うファイルなら違う番号」が破れ，時計の無い環境で
 * st_mtime に 0 を置けば「本当の暦の値か」が破れる。
 *
 * **ホストにしか無い欄は見ない。** st_mtlo / st_mthi は我々の欄なので，
 * ここで触るとホスト側が翻訳できない。上位語が効いていることは
 * st_mtime が 10 億を超えることで判る (ナノ秒で 10 億秒は 62 bit 目に
 * 届くので，下位語だけからは作れない)。
 *
 * ファイルは走らせた場所に作る。ホスト側は作業場の中で走らせる
 * (tools/diff17.sh)。 */
#include <stdio.h>
#include <sys/stat.h>

/* y / n で答える。値ではなく**性質**を出す */
static void yn(char *tag, int v) { printf("%s %s\n", tag, v ? "y" : "n"); }

/* path へ s を書き，書けたバイト数を返す (失敗なら -1) */
static int mkfile(char *path, char *s) {
  FILE *f;
  int n;
  n = 0;
  while (s[n]) n = n + 1;
  f = fopen(path, "w");
  if (f == 0) return -1;
  if ((int)fwrite(s, 1, (unsigned)n, f) != n) { fclose(f); return -1; }
  if (fclose(f) != 0) return -1;
  return n;
}

int main(void) {
  struct stat a;
  struct stat a2;
  struct stat b;
  struct stat d;
  struct stat z;

  yn("mkfile", mkfile("sx_a.txt", "hello\nworld") == 11
             && mkfile("sx_b.txt", "x") == 1);

  /* ディレクトリは既にあるかもしれない。**戻り値は見ない** ——
   * 1 度目と 2 度目で違うので，突き合わせる材料にならない */
  mkdir("sx_d", 0755);

  yn("stat_a", stat("sx_a.txt", &a) == 0);
  yn("stat_a2", stat("sx_a.txt", &a2) == 0);
  yn("stat_b", stat("sx_b.txt", &b) == 0);
  yn("stat_d", stat("sx_d", &d) == 0);
  yn("nosuch", stat("sx_none.txt", &z) != 0);

  /* ---- 長さ。前から持っていた欄である ---- */
  yn("size_a", a.st_size == 11);
  yn("size_b", b.st_size == 1);

  /* ---- st_ino。同じファイルなら同じ，違えば違う ---- */
  yn("ino_same", a.st_ino == a2.st_ino);
  yn("ino_diff", a.st_ino != b.st_ino);
  yn("ino_dir", a.st_ino != d.st_ino);
  /* **0 を配らない。** 0 を「番号が無い」の意で使う呼び手がある */
  yn("ino_nz", a.st_ino != 0 && b.st_ino != 0 && d.st_ino != 0);

  /* ---- st_dev。1 つの巻の上なら揃う ---- */
  yn("dev_same", a.st_dev == b.st_dev && a.st_dev == d.st_dev);
  yn("dev_nz", a.st_dev != 0);

  /* ---- st_mode。種別のビットだけを見る ---- */
  yn("reg_a", S_ISREG(a.st_mode) != 0);
  yn("reg_d", S_ISREG(d.st_mode) == 0);
  yn("dir_d", S_ISDIR(d.st_mode) != 0);
  yn("dir_a", S_ISDIR(a.st_mode) == 0);
  /* 種別は 1 つだけ立つ */
  yn("one_a", (S_ISREG(a.st_mode) != 0) + (S_ISDIR(a.st_mode) != 0) == 1);
  yn("one_d", (S_ISREG(d.st_mode) != 0) + (S_ISDIR(d.st_mode) != 0) == 1);

  /* ---- st_mtime。**本当の暦の値か** ----
   *
   * 2001-09-09 (1000000000) より後であることを見る。時計が無い環境の
   * 0 や，起動からの経過を秒に直しただけの小さな値はここで落ちる */
  yn("mt_real", a.st_mtime > 1000000000);
  /* b は a より後に書いた。**同じ秒に収まることがある**ので >= で見る */
  yn("mt_order", b.st_mtime >= a.st_mtime);
  return 0;
}
