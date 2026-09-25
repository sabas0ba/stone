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
/* ---- 第 24 世代で足した欄 (docs/stage017-gcc.md 8.5) ----
 *
 * GCC 4.7.4 の 5 単位が `st_dev` / `st_ino` / `st_mode` / `st_mtime` で
 * 止まっていた (libiberty/fdmatch, libiberty/getpwd,
 * libiberty/unlink-if-ordinary, libcpp/files, libcpp/macro)。
 *
 * **4 つとも本当の値である。** 埋め草を置いて名前だけ揃えたものは
 * 1 つも無い。持っていない欄 (所有者・許可ビット) は依然として
 * 構造体に無く，**許可の macro も定義していない** —— 使おうとすれば
 * 翻訳が通らないので，そこで気づける。
 *
 *   st_dev    **常に 1。** sfs の巻は 1 つしか無いので，見えている
 *             ファイルは全部同じ装置の上にある。「同じ装置か」の答は
 *             常に「はい」であり，これは埋め草ではなく事実である。
 *             **2 つめの巻を繋ぐ日には本物にすること** —— そのときに
 *             ここが嘘になる
 *   st_ino    sfs の表の項目番号 + 1。1 つの巻の中で一意で，名前を
 *             変えても追える。+1 は 0 を「番号が無い」の意で使う
 *             呼び手のためである。カーネルの statat2 (502) が配る
 *   st_mode   **種別のビットだけ**。S_IFDIR か S_IFREG が立つ。
 *             許可ビットは 0 だが，**読む道具を置いていない** ——
 *             S_IRUSR などは定義していないので、許可を見ようとする
 *             ソースは翻訳の時点で止まる
 *   st_mtime  epoch からの秒。ナノ秒の 64 bit を 10^9 で割って作る。
 *             カーネルの時計は goldfish RTC で，**本当の暦の時刻**で
 *             ある (kernel24 以降)
 *
 * **`time()` と `localtime()` は別物である。** どちらもまだ紀元の
 * 固定値を返す (include/time.h)。`st_mtime` が本物になっても
 * `__TIMESTAMP__` の類は直らない —— そちらは `asctime` も要る。
 */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#define S_TYPE_DIR 1
#define S_TYPE_REG 2

/* **見張りを付ける** —— time.h と sys/types.h も同じ型を持つ */
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

/* 種別のビット (POSIX と同じ値)。**許可ビットは配らない** */
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000

#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
/* 持っていない種別は**常に偽**である。sfs にはこれらが無いので，
 * 「無い」と答えるのが本当の答である */
#define S_ISLNK(m)  (0)
#define S_ISCHR(m)  (0)
#define S_ISBLK(m)  (0)
#define S_ISFIFO(m) (0)
#define S_ISSOCK(m) (0)

struct stat {
  long st_size;                 /* 長さ (バイト) */
  unsigned st_mtlo;             /* 更新時刻 (ns) の下位 32 bit */
  unsigned st_mthi;             /* 同 上位 32 bit */
  int st_type;                  /* S_TYPE_DIR か S_TYPE_REG */
  int st_dev;                   /* 巻。常に 1 (上の註) */
  long st_ino;                  /* sfs の表の項目番号 + 1 */
  int st_mode;                  /* 種別のビットだけ */
  time_t st_mtime;              /* epoch からの秒 */
};

int mkdir(char *path, int mode);

/* 成功なら 0，無ければ -1 (errno = ENOENT) */
int stat(char *path, struct stat *st);

/* a の時刻が b より新しければ 1。同じか古ければ 0。
 * **上位語から見る。** 下位だけ見ると桁上がりで逆転する */
int newer(struct stat *a, struct stat *b);

#endif
