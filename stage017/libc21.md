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

`bzip2` の**命令そのもの**を組むには，あと 6 つ要る (32.4)。

| | 我々の側の仕事 |
|---|---|
| `utime` | **カーネルに要る**。sfs3 は時刻を持つが，外から設定する syscall が無い |
| `fchmod` | sfs に許可は無い。要求が常に成り立っているので 0 を返す形になる |
| `fileno` / `getenv` / `isatty` / `perror` | libc だけで済む |
