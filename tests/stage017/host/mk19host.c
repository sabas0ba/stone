/* mk19host.c --- mk19 をホストで走らせるための身代わり。
 *
 * mk17 のときの mkhost.c と同じ考えで，**mk19.c そのものを**取り込む
 * (mk19.c の側に検査用の分岐を入れない)。違いは 2 つある。
 *
 *   1. spawn2 を system() へ回す (mkhost.c と同じ)
 *   2. **libc19 の stat / newer をホストの本物から作る**
 *
 * 2 が要るのは，mk19 が sfs3 の時刻を見るからである。ホストには
 * libc19 が無く，`struct stat` はホストのものが入っている。そこで
 *
 *   - 先に本物の <sys/stat.h> を取り込んで，本物の stat() で mtime を
 *     取る助けを 1 つ書く
 *   - そのあと `#define stat mk_stat` などで名前を替える。C では
 *     構造体タグと関数が別の名前空間だが，**マクロは綴りを替えるので
 *     `struct stat` も `struct mk_stat` になる**。これでホストの型と
 *     ぶつからずに libc19 と同じ形を用意できる
 *
 * mk19.c の中の #include は，先にここで取り込んでおけば何もしない。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* 本物の stat で長さ・時刻・種別を取る */
static int host_stat(const char *path, long *sz, unsigned *lo, unsigned *hi,
                     int *type) {
  struct stat s;
  unsigned long long ns;
  if (stat(path, &s) < 0) return -1;
  ns = (unsigned long long)s.st_mtim.tv_sec * 1000000000ull
       + (unsigned long long)s.st_mtim.tv_nsec;
  *sz = (long)s.st_size;
  *lo = (unsigned)(ns & 0xffffffffull);
  *hi = (unsigned)(ns >> 32);
  *type = S_ISDIR(s.st_mode) ? 1 : 2;
  return 0;
}

/* ここから先は libc19 の形。**綴りを替えてホストの型と分ける** */
#define S_TYPE_DIR 1
#define S_TYPE_REG 2

struct mk_stat {
  long st_size;
  unsigned st_mtlo;
  unsigned st_mthi;
  int st_type;
};

/* **タグと関数が同じ綴りなのは libc19 と揃えるためである。** C では
 * 構造体タグと関数は別の名前空間なので，これで通る */
static int mk_stat(char *path, struct mk_stat *st) {
  return host_stat(path, &st->st_size, &st->st_mtlo, &st->st_mthi,
                   &st->st_type);
}

static int mk_newer(struct mk_stat *a, struct mk_stat *b) {
  if (a->st_mthi != b->st_mthi) return a->st_mthi > b->st_mthi;
  return a->st_mtlo > b->st_mtlo;
}

static int spawn2(char *path, char **argv, char *in, char *out, char *err);

#define stat  mk_stat
#define newer mk_newer
#include "../../../stage017/mk19.c"
#undef stat
#undef newer

static int spawn2(char *path, char **argv, char *in, char *out, char *err) {
  char cmd[1024];
  int r;
  (void)path;
  (void)in;
  (void)out;
  (void)err;
  strcpy(cmd, "sh ");
  strcat(cmd, argv[1]);
  r = system(cmd);
  if (r == -1) return 127;
  return (r >> 8) & 0xff;
}
