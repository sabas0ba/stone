# libc26 --- GCC の cc1 が使う関数を足した世代 (第 26 世代)

`libc25` の全文複製に，**GCC 4.7.4 の cc1 をリンクして名指しされた不足**を足したものである。経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.15。

触っているのは次のファイルだけで，他は `libc25` と 1 バイトも変わらない。

| ファイル | 変更 |
|---|---|
| `src/misc26.c` | 新規。環境に依らない関数 |
| `posix/sys26.c` | 新規。カーネルと stdio に依る関数 |
| `include/float.h` | 新規。浮動小数点型の特性 (C89 5.2.4.2.2) |
| `include/alloca.h` | 新規。`alloca` の宣言 (glibc と同じく stdlib.h には置かない) |
| `include/stdlib.h` / `string.h` / `stdio.h` / `unistd.h` / `time.h` / `math.h` / `sys/stat.h` | 末尾に宣言を足した |
| `include/fcntl.h` / `posix/sys.c` | `O_EXCL`，`F_GETFD` / `F_SETFD`。`fcntl` を可変引数にした (下の 5 章) |

## 1. 立場

**この世代から作られる記録対象の成果物はまだ無い。** したがって本書に SHA-256 の表は無い ([artifacts.md](../docs/artifacts.md))。

**カーネルの第 28 世代が要る。** `fstat()` が `fstat2` (503) を呼ぶ (stage017/kernel28.c)。`times()` が `times` (153) を呼ぶのは `libc25` と同じである。

`libc25` までの部品は `cc15ag` で訳していた。`libc26` は cc1 の単位と同じく最前線の cc (`cc15am`) で訳す。遠距離呼出しの行は付けない —— libc の部品は前置部の syscall スタブを `jal` で呼ぶので，前置部の近く (1 MiB 以内) に置く (stage015/ld18.md の「置く順」)。

## 2. 足したもの

### C89 の関数

| 関数 | 内容 |
|---|---|
| `abort` | `SIGABRT` を上げ，戻れば 134 (128 + 6) で終わる |
| `labs` / `atol` / `atof` | `strtol` / `strtod` を包む |
| `strtok` | 区切りの並びで切る。空の欄は飛ばす |
| `frexp` | 指数の欄を書き換えて仮数を [0.5, 1) に入れる。非正規化数は 2^54 を掛けて正規化してから分ける |
| `asctime` | `"Sun Sep 16 01:03:52 1973\n"` の形 (C89 7.12.3.1) |
| `float.h` | `FLT_*` / `DBL_*` / `LDBL_*`。**long double は double と同じ 8 バイト**なので `LDBL_*` は `DBL_*` と同じ値である。MPFR の configure が必須とする |

### POSIX の関数

| 関数 | 内容 |
|---|---|
| `fileno` / `freopen` | `freopen` は新しく開いた流れを同じ `FILE` の枠へ写し，前の記述子を閉じる |
| `fstat` | 下の 3 章 |
| `lstat` | sfs に記号リンクは無いので `stat` と同じ |
| `access` | 在れば 0。sfs に許可は無く，在るファイルはどれも読み書きでき `spawn` で起動できるので，`mode` は答を変えない |
| `strcasecmp` / `strncasecmp` | ASCII の大文字・小文字を同じと見て比べる |
| `environ` | カーネルは環境を渡さないので，常に空 (NULL だけ) |

### cc1 の configure が「有る」とした拡張

GCC の configure は関数の有無を host (glibc) へのリンクで決めていたので，glibc の拡張も「有る」と書いた config.h で訳していた。gcc17.sh の configure は 8.15 から我々の libc の記号表で答えるが (`func_cache`)，次の関数は libc に置いた。

| 関数 | 内容 |
|---|---|
| `alloca` | 下の 4 章 |
| `canonicalize_file_name` | `realpath(path, NULL)` と同じ |
| `getpagesize` | 4096。libc の `morecore` が `sbrk` で取る最小の単位である。MMU の頁ではない (このカーネルは MMU を使わない) |
| `stpcpy` / `strnlen` | POSIX 2008 の関数 |

## 3. `fstat` —— カーネルが記述子で答える

カーネルは記述子ごとに表の項目番号 (`fdent`) を持っている。kernel28 の `fstat2` (503) はその項目を引き，`statat2` (502) と同じ 5 語を書く。返すのは `stat` と同じ本当の値である。

**開いたまま unlink したファイルも答えられる。** カーネルの `unlinkat` は開いている項目の名前と親だけを消し，実体を残す。`fstat` は記述子から同じ項目を引く。

第 1 版は `open` が開いた経路を控えて `stat` していたので，開いた後に unlink すると `ENOENT` になっていた (自動レビューの指摘)。経路に依る作りをやめ，カーネルに 1 本足した。

0 / 1 / 2 (端末) は表の項目を持たないので -1 (`errno = EBADF`) を返す。端末の種別を表す `S_IFCHR` を `sys/stat.h` が配っていないので，答えられる値が無い。

## 4. `alloca` —— cc が呼んだ関数の戻りで返す

本物の `alloca` はスタックを伸ばすが，我々の cc は関数の枠の大きさを翻訳時に決めるので，呼出しで伸ばせない。**ヒープから取り，取った関数の枠の基底を控える。**

cc15ao 以降の cc は `alloca(n)` を `__alloca2(n, fp)` に書き換え，alloca を呼んだ関数が戻る直前に `__alloca_release(fp)` を呼ぶ (stage015/cc15ao.md)。fp は呼んだ関数の枠の基底である。`__alloca_release` は基底が fp 以下 (その呼出しと，より深い呼出し) の領域を返す。寿命は本物と同じく「呼んだ関数が戻るまで」になる。

第 1 版は名前で呼ばれる `alloca` だけで，取ったときの alloca 自身の枠の位置を控え，次の呼出しで今より深い所の分を返していた。同じ深さで繰り返し呼ばれる関数 (ループの中から呼ぶ関数) の分は，より浅い所から alloca が呼ばれるまで返らない (自動レビューの指摘)。名前で呼ぶ `alloca` (関数へのポインタを通した呼出しや，書き換えない cc で訳したもの) にはこの性質が残る。

戻った後に触る使い方は本物と同じく誤りである。

## 5. `O_EXCL` と close-on-exec —— libiberty の 2 単位

libiberty の `mkstemps.c` は `O_EXCL` を，`pex-unix.c` は `fcntl (fd, F_SETFD, FD_CLOEXEC)` を #ifdef で守らずに使う。`libc25` はどちらも配っていなかったので，2 単位が宣言の不足で訳せなかった。

**`O_EXCL`** (`O_CREAT` と組で，既にあれば `EEXIST` で失敗する)。カーネルの `openat` はこのフラグを知らないので，`open()` が先に `stat` して確かめ，フラグはカーネルへ渡さない。確かめてから作るまでの間に他が割り込むことは無い。走行は逐次で，`spawn` は子の終わりを待つ (助言的ロックが常に取れるのと同じ理由)。`libc25` の `fcntl.h` は「無いものは無いと言う」ために `O_EXCL` を定義していなかったが，意味どおりに実装できたので定義する。

**close-on-exec。** kernel28 の `spawn` は子の記述子の表を 3 以上すべて空にして始める。どの記述子も既に close-on-exec である。`F_GETFD` は常に `FD_CLOEXEC` を返し，`F_SETFD` は `FD_CLOEXEC` を立てる依頼だけを受ける。落とす依頼 (子に渡したい) は叶えられないので `EINVAL` で拒む。

**`fcntl` を可変引数にした。** 第 3 引数は命令によって整数 (`F_SETFD`) か `struct flock *` (ロック) である。`libc25` は `void *arg` で宣言していたので，整数を渡す呼出しが型で合わなかった。POSIX の宣言 `int fcntl(int, int, ...)` に合わせる。ロックの依頼は `va_arg` でポインタとして受ける。

## 6. 測り方

`tests/stage017` 第 12 部が `tests/stage017/user/l26x.c` を 最前線の cc (cc15aq) の遠距離呼出しで訳し，ld18 で libc26 をライブラリの部品として組んで kernel28 の上で走らせる。

- `alloca`: 同じ深さで alloca を呼ぶ関数を 10 万回呼ぶ。1 回 4000 バイトなので，戻りで返さなければ 400 MB を取ろうとしてヒープ (プロセスの領域は 256 MiB) が尽きる。cc15an で訳すと `abort` で 134 になる。
- `fstat`: 開いたファイルを unlink した後に `fstat` し，書いた長さが返ることを見る。
- `O_EXCL`: 在るファイルは `EEXIST` (17) で拒み，無いファイルは作る。`F_GETFD` は 1，`F_SETFD` は立てる依頼に 0，落とす依頼に -1 を返す。 ポインタを渡すロック (`F_SETLK` / `F_GETLK`) が可変引数の後も受かることも見る。
