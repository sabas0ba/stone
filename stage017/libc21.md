# libc21 --- 実物が読むものを足した世代 (第 21 世代)

`libc20` の全文複製に，**実物 (zlib / bzip2) が読むのに我々が持って
いなかったもの**を足した世代である。経緯は
[docs/stage017-cc.md](../docs/stage017-cc.md) 32 章。

## 1. 立場

**この世代から作られる記録対象の成果物はまだ無い。**

鎖の側 (`cc19` が tcc を組む道) は `libc20` のままである。そちらを
動かすと 21 章・29 章のバイト突き合わせが動くので，触っていない。
`libc21` を使うのは **tcc の世界だけ** —— `tools/tcc17.sh` の
`oslibc` / `ext` が，このソースを **tcc に訳させて** `/usr/lib/libc.a`
を作る (30〜32 章)。

したがって本書に SHA-256 の表は無い。表が要るのは，この世代から
記録対象の成果物を作るようになったときである
([docs/artifacts.md](../docs/artifacts.md) 4)。

## 2. `libc20` との差

| | 誰が要ったか |
|---|---|
| `include/sys/types.h` (新) | zlib の `zconf.h` 446 行 (`off_t`) |
| `include/signal.h` (新) | bzip2 の `bzip2.c` 53 行 |
| `posix/signal.c` (新) | 同上の実体 |
| `include/fcntl.h` に `O_APPEND` | zlib の `gzlib.c` 228 行 |
| `posix/sys.c` の `open()` が `O_APPEND` を拒む | 上と対 |
| `src/string.c` に `memchr` | zlib の `gzread.c` 538 行 |
| `include/string.h` に `memchr` / `strerror` の宣言 | zlib の `gzread.c` / `gzwrite.c` |
| `include/unistd.h` に `lseek` の宣言 | zlib の `gzlib.c` 242 行 |
| `include/time.h` / `include/unistd.h` に型の見張り | `sys/types.h` と二重定義になるため |
| `posix/stdio.c` の `fopen(path, "a")` が**本当に追記する** | 下の 3.1 |

### 3.1 追記 ("a") が直った

第 20 世代までの `fopen(path, "a")` は `O_WRONLY | O_CREAT` で開くだけ
だった。カーネルは記述子の位置を 0 から始めるので，**既存のファイルの
先頭から上書きしていた**。[stage014/libc.md](../stage014/libc.md) の
「制限」にそう書いてあり，そこには

> 追記が要るなら `fopen` の前に長さを調べて自前で位置を進める，という
> 逃げ道も無い——**seek 系の syscall がそもそも無い**

ともあった。**その前提はもう成り立たない。** `lseek` (62) が入っている
(docs/stage017-cc.md 11 章)。

そこで `FILE` に `app` を足し，**書く前に必ず末尾へ寄せる**形で追記を
libc の側に実装した。我々は単一の走行なので (spawn は子の終わりを待つ)，
これで POSIX の `O_APPEND` と同じ意味になる —— 途中で `fseek` しても，
次に書くのは末尾である。

**生の `O_APPEND` は `open()` が拒むままにする。** カーネルが知らない旗を
黙って捨てるためで，そちらは実装していない。`fopen` は `O_APPEND` を
渡さず，印だけ立てる。

## 3. 実装しないものの扱い

**無いものは無いと言う。** 名前だけ与えて黙って通すと，呼び手は
「効いている」と思い込む。

| | どうしたか | なぜ |
|---|---|---|
| `signal()` | 登録を受け付け，**何も起こさない**。返り値は常に `SIG_DFL` | シグナルが起きない環境では，手当てが呼ばれないことが正しい振舞いである |
| `raise()` | `EINVAL` で**拒む** | 0 を返すと「送ったのに何も起きない」という嘘になる |
| `O_APPEND` | 名前は与え，`open()` が `EINVAL` で**拒む** | カーネルは知らない旗を黙って捨てるので，渡すと「追記のつもりが先頭から上書き」になる |
| `O_EXCL` / `O_CLOEXEC` | **定義しない** | zlib はどちらも `#ifdef` で守る。無ければその道が落ちて正しく動く |

## 4. まだ足りないもの

`bzip2` の**命令そのもの**は組めない。止まるのは `utime.h` が無いこと
だが，そこを埋めても続かない —— **本当の壁は `struct stat` である**
(32.4)。

`bzip2.c` は元のファイルの素性を写すために `st_mode` / `st_nlink` /
`st_atime` / `st_uid` / `st_gid` を読み，`fchmod` / `fchown` / `utime`
で書く。我々の `struct stat` は**長さ・更新時刻・種別しか持たない**。
これは足りていないのではなく，2 章の方針どおり**そう決めてある**。

欄を足せば組めるが，sfs3 がその値を持っていないので返すのは 0 に
なる。すると `bzip2` は「許可を写した」と思って進む。**通るが誤って
いる**という，台帳で `bad` と呼ぶ状態になる。

| | 要るもの | 大きさ |
|---|---|---|
| 1 | sfs が許可・所有者・参照数・参照時刻を持つ | **ファイル系の世代** |
| 2 | `utime` / `fchmod` / `fchown` の syscall | **カーネルの世代** |
| 3 | `struct stat` に欄を足す | 1 が済んでから |
| 4 | `fileno` / `getenv` / `isatty` / `perror` | libc だけで済む |

**4 だけ足しても組めない。組めるようにするために嘘の欄を足すことは
しない。**
