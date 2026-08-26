/* sys/types.h --- POSIX の基本型 (第 21 世代)
 *
 * 実物が読むために足した。zlib の zconf.h 446 行が
 *
 *     #ifdef STDC
 *     #  ifndef Z_SOLO
 *     #    include <sys/types.h>      // for off_t
 *
 * と書いていて，これが無いと **zlib の全単位が 1 行目で止まる**
 * (docs/stage017-cc.md 32 章)。
 *
 * 幅は我々の対象 (RV32) に合わせる。**64 bit の off_t は持たない** ——
 * sfs のファイルは 32 bit で足りる (255 MiB の記憶域)。zlib は
 * _LARGEFILE64_SOURCE を定義しなければ off_t をそのまま使う。
 *
 * size_t / ptrdiff_t は stddef.h の側が持つ。ここで二重に typedef すると
 * 我々の cc が拒むので，**stddef.h を読み込んで借りる**。
 */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

/* ssize_t と time_t は unistd.h / time.h も持つ。**両方を読むソースが
 * ある** (zlib) ので、見張りを揃えて二重の typedef を避ける */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int ssize_t;            /* read / write の返り値 */
#endif
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

typedef int off_t;              /* ファイル内の位置。lseek の第 2 引数 */
typedef int mode_t;             /* open の第 3 引数 (sfs に許可は無い) */
typedef int pid_t;              /* spawn が返す */
typedef int uid_t;
typedef int gid_t;
typedef unsigned ino_t;         /* sfs の表の索引 */
typedef unsigned dev_t;
typedef unsigned nlink_t;

#endif
