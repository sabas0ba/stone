/* sys/stat.h --- ディレクトリの作成と stat (POSIX。第 19 世代)
 *
 * 実装は posix/sys.c。mkdir はカーネルの mkdirat (34) を，stat は
 * statat (79) を包む (docs/stage017-cc.md 11.5)。
 *
 * ---- 第 18 世代までは stat が無かった ----
 *
 * libc18 の同じヘッダにはこう書いてあった。
 *
 *   **stat / fstat は無い。** sfs2 に許可・所有者・時刻が無く，返せる
 *   欄がほとんど 0 になる。「あるのに嘘の値を返す」より「無い」ほうが
 *   呼び手にとって安全である
 *
 * sfs3 が時刻を持ったので，この判断の前提が変わった。**持つ。**
 * ただし返すのは**本当に持っている欄だけ**である。
 *
 * **st_uid / st_gid / 許可ビットは置いていない。** 持っていない欄を
 * 0 で埋めて名前だけ本物に揃えると，呼び手が「見た」つもりになる。
 * 無い欄は構造体に無いのがいちばん安全である —— 使おうとすれば
 * 翻訳が通らないので，そこで気づける。
 *
 * 時刻は **epoch からのナノ秒を u32 2 本**で持つ。秒に直さないのは，
 * 直すのに 64 bit の除算が要るからである (11.2)。比べるときは
 * 上位語 -> 下位語の 2 段で見る。
 *
 * mkdir の mode は受けて捨てる。sfs3 に許可の概念が無い。
 */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#define S_TYPE_DIR 1
#define S_TYPE_REG 2

struct stat {
  long st_size;                 /* 長さ (バイト) */
  unsigned st_mtlo;             /* 更新時刻 (ns) の下位 32 bit */
  unsigned st_mthi;             /* 同 上位 32 bit */
  int st_type;                  /* S_TYPE_DIR か S_TYPE_REG */
};

int mkdir(char *path, int mode);

/* 成功なら 0，無ければ -1 (errno = ENOENT) */
int stat(char *path, struct stat *st);

/* a の時刻が b より新しければ 1。同じか古ければ 0。
 * **上位語から見る。** 下位だけ見ると桁上がりで逆転する */
int newer(struct stat *a, struct stat *b);

#endif
