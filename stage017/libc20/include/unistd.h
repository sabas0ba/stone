/* unistd.h --- syscall の薄い包み (POSIX 風。第 13 世代)
 *
 * 実装は lib/posix/sys.c。前置部が提供する生のスタブ (sys_read など) を
 * 呼び，失敗 (-errno) を errno へ写して -1 を返す
 * (docs/stage012-os.md 6.3)。
 *
 * 非目標: fork / exec / dup / pipe。プログラムの起動は spawn
 * (子の終わりを待つ逐次実行。docs/stage013-tools.md 3.2) だけを持つ。
 */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

/* 符号つきの大きさ (POSIX)。第 6 部の実測: tcc の full_read が使う */
typedef int ssize_t;

int read(int fd, void *buf, size_t n);
int write(int fd, void *buf, size_t n);
int close(int fd);

/* 作業ディレクトリ (第 16 世代。docs/stage016-os.md 7.3)。
 * getcwd は buf を返す。足りなければ errno = ERANGE で NULL */
int chdir(char *path);
char *getcwd(char *buf, size_t size);

/* ファイルを消す (第 17 世代。docs/stage016-os.md 9.4)。
 * ディレクトリには効かない (EISDIR)。開いている fd はそのまま使える */
int unlink(char *path);

/* 記憶域の末尾を n バイト伸ばし，伸ばす前の末尾を返す。失敗は (void *)-1 */
void *sbrk(int n);

/* path を起動して終わりを待ち，子の終了コード (0..255) を返す。失敗は -1。
 * argv は NULL 終端 (NULL なら {path} 相当)。in / out は子の標準入出力を
 * 結ぶ sfs ファイル名 (NULL なら親と同じ先を継ぐ) */
int spawn(char *path, char **argv, char *in, char *out);

/* spawn に標準エラーの行き先を足したもの (第 18 世代)。
 * err が NULL なら親と同じ先を継ぐ (= 端末) */
int spawn2(char *path, char **argv, char *in, char *out, char *err);

/* 走行の番号 (第 20 世代)。
 *
 * **我々に番号は無い。** カーネルに getpid の syscall が無く，走行は
 * 逐次なので区別する相手も居ない。tcc の tcov.c は一時ファイルの名前を
 * 他の走行とぶつけないために使うが，ぶつかる相手が居ない。
 * 常に 1 を返す。**0 を返さない**のは，0 を「番号が無い」の意味に
 * 使う書き方があるためである */
int getpid(void);

#endif
