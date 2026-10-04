# libc26 --- GCC の cc1 が使う関数を足した世代 (第 26 世代)

`libc25` の全文複製に，**GCC 4.7.4 の cc1 をリンクして名指しされた不足**を足したものである。経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.15。

触っているのは次のファイルだけで，他は `libc25` と 1 バイトも変わらない。

| ファイル | 変更 |
|---|---|
| `src/misc26.c` | 新規。環境に依らない関数 |
| `posix/sys26.c` | 新規。カーネルと stdio に依る関数 |
| `posix/sys.c` | `open` / `close` が記述子の経路を控える (`fstat` のため) |
| `include/float.h` | 新規。浮動小数点型の特性 (C89 5.2.4.2.2) |
| `include/alloca.h` | 新規。`alloca` の宣言 (glibc と同じく stdlib.h には置かない) |
| `include/stdlib.h` / `string.h` / `stdio.h` / `unistd.h` / `time.h` / `math.h` / `sys/stat.h` | 末尾に宣言を足した |

## 1. 立場

**この世代から作られる記録対象の成果物はまだ無い。** したがって本書に SHA-256 の表は無い ([artifacts.md](../docs/artifacts.md))。

**カーネルの第 27 世代が要る** (`libc25` と同じ。`times()` が `times` (153) を呼ぶ)。

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

## 3. `fstat` —— 記述子の経路を libc が控える

**カーネルに記述子の stat は無い** (`statat` は経路で引く)。カーネルに手を入れる代わりに，`open` が開いた経路を**絶対形で**控え，`fstat` はその経路を `stat` する。相対のまま控えると，開いた後に `chdir` した場合に別のファイルを引く。

返すのは `stat` と同じ本当の値である。経路を持たない記述子 (0 / 1 / 2 の端末) は -1 (`errno = EBADF`) を返す。端末の種別を表す `S_IFCHR` を `sys/stat.h` が配っていないので，答えられる値が無い。

**限界。** 開いた後に経路の名前を変えると，`fstat` は新しい名前を知らない。sfs4 には名前を変える syscall が無いので，今は起こらない。

## 4. `alloca` —— ヒープから取り，戻った関数の分を返す

本物の `alloca` はスタックを伸ばすが，我々の cc は関数の枠の大きさを翻訳時に決めるので，呼出しで伸ばせない。**ヒープから取り，取ったときのスタックの深さを控える。** 次に呼ばれたとき，今より深い所 (番地が小さい所。スタックは下へ伸びる) で取った分は，その関数が既に戻っているので返す。同じ関数から何度呼んでも深さは同じなので返さない。

戻った後に触る使い方は本物と同じく誤りである。違うのは，返すのが次の `alloca` の呼出しまで遅れることだけである。`alloca(0)` は返すだけを行う。

## 5. 測り方

`tests/stage017` 第 12 部が `tests/stage017/user/l26x.c` を遠距離呼出しで訳し，ld18 で libc26 をライブラリの部品として組んで kernel27 の上で走らせる。`alloca` は 20 段の再帰で 1000 バイトずつ取る呼出しを 200 回繰り返し，戻った関数の分を返さなければ 4 MB を超えて取り続ける形で見る。
