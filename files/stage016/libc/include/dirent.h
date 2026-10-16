/* dirent.h --- ディレクトリの読み出し (POSIX。第 16 世代)
 *
 * 実装は posix/dir.c。カーネルの getdents64 (61) を包む
 * (docs/stage016-os.md 7.2)。
 *
 * `struct dirent` は POSIX が要求する `d_name` に，あると便利な
 * `d_ino` と `d_type` を足しただけである。`d_type` は POSIX には無いが
 * 実装が広く持っており，これが無いと「一覧の各項目がディレクトリか」を
 * 知るのに毎回 stat が要る。我々には stat が無いので，ここで持つ。
 *
 * `.` と `..` は返さない。sfs2 に実体が無く，POSIX も「dot / dot-dot を
 * 返すかどうかは未規定」としている。
 *
 * 非目標: seekdir / telldir / rewinddir / scandir。
 */
#ifndef _DIRENT_H
#define _DIRENT_H

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
  unsigned long d_ino;
  unsigned char d_type;
  char d_name[256];
};

/* 器は 1 つの DIR につき固定で持つ。getdents64 は 1 回の呼出しで
 * 入るだけ返すので，読み切ってから次を取りにいく */
#define _DIRBUF 1024

typedef struct {
  int fd;
  int len;                      /* buf に入っている有効なバイト数 */
  int pos;                      /* 次に取り出す位置 */
  struct dirent ent;            /* readdir が返す実体 (呼出しごとに上書き) */
  char buf[_DIRBUF];
} DIR;

DIR *opendir(char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);

#endif
