/* ホストで我々のシェル (sh5) を組むための `spawn2` (tools/diffglob.sh)。
 *
 * `sh5.c` は外部コマンドの起動を `spawn2(path, argv, in, out, err)` 1 本に
 * 集めている。これは我々のカーネルの呼出し (501) で、ホストには無い。
 * **経路展開そのものはカーネルに依らない**ので、ここを塞げば同じソースを
 * ホストでも組めて、ホストのシェルと突き合わせられる。
 *
 * ---- 合わせた振舞い ----
 *
 * `kernel25.c` の `sys_spawn` に合わせる。
 *
 *   in    NULL なら親の標準入力をそのまま継ぐ。名前があれば読みで開く
 *   out   NULL なら親の標準出力を継ぐ。名前があれば**切り詰めて**開く
 *         (無ければ作る)
 *   err   同上。`spawn2` だけが持つ 5 語目である
 *
 * 返すのは子の終了状態である。
 *
 * **これは物差しの側の足場であって、鎖の成果物ではない。**
 * `tests/hostshim/shim.h` と同じ立場のものである。 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int redirect(const char *path, int flags, int fd)
{
    int f;

    if (path == NULL)
        return 0;
    f = open(path, flags, 0666);
    if (f < 0)
        return -1;
    if (dup2(f, fd) < 0) {
        close(f);
        return -1;
    }
    close(f);
    return 0;
}

int spawn2(char *path, char **argv, char *in, char *out, char *err)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (redirect(in, O_RDONLY, 0) < 0)
            _exit(127);
        if (redirect(out, O_WRONLY | O_CREAT | O_TRUNC, 1) < 0)
            _exit(127);
        if (redirect(err, O_WRONLY | O_CREAT | O_TRUNC, 2) < 0)
            _exit(127);
        execvp(path, argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + WTERMSIG(status);
}
