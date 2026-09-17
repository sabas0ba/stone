/* signal.h --- シグナル (第 21 世代。**中身は無い**)
 *
 * **我々の OS にシグナルは無い。** それでも実物はこのヘッダを読む ——
 * bzip2 の bzip2.c 53 行が読み，1808 行で
 *
 *     signal (SIGSEGV, mySIGSEGVorSIGBUScatcher);
 *
 * と書く。無いと**行 53 で止まる** (docs/stage017-cc.md 32 章)。
 *
 * ## 何を約束するか
 *
 * `signal()` は **登録を受け付けて，何も起こさない**。返り値は直前の
 * 扱い (常に SIG_DFL) である。`raise()` は EINVAL で拒む。
 *
 * これは「動くふり」ではない。**シグナルが起きない環境では，登録した
 * 手当てが呼ばれないことが正しい振舞いである**。bzip2 が登録するのは
 * SIGSEGV / SIGBUS / SIGINT / SIGTERM / SIGHUP で，どれも
 *
 *   - SIGSEGV / SIGBUS  … 我々では不正な参照はカーネルの捕捉に落ちる
 *   - SIGINT / SIGHUP   … 端末から割り込む道が無い
 *   - SIGTERM           … 他の処理を殺す道が無い
 *
 * なので，呼ばれないことが実際に起きることと合っている。
 *
 * **本当に手当てが要る形が出てきたら，そのときは OS の側に足す。**
 * ここで嘘を書かないために raise() は拒む形にしてある。
 *
 * 番号は Linux の値に揃える (docs/stage012-os.md 2.2 の方針)。
 */
#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef void (*__sighandler_t)(int);

#define SIG_DFL ((__sighandler_t)0)
#define SIG_IGN ((__sighandler_t)1)
#define SIG_ERR ((__sighandler_t)-1)

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV 11
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15

/* C89 7.7.1。実物は typedef を経由しない形も書くが，呼ぶ側から見た
 * 型は同じである */
__sighandler_t signal(int sig, __sighandler_t func);
int raise(int sig);

#endif
