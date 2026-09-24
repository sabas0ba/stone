# libc25 --- プロセスの時間と経路の上限を足した世代 (第 25 世代)

`libc24` の全文複製に、**GCC 4.7.4 の libiberty 2 単位を止めていた
2 つの不足**に対応したものである。経緯は
[docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.9。

触っているのは次の 4 本だけで、他は `libc24` と 1 バイトも変わらない。

| ファイル | 変更 |
|---|---|
| `include/sys/times.h` | 新規。`struct tms` / `clock_t` / `times()` |
| `include/unistd.h` | `_SC_CLK_TCK` / `_PC_PATH_MAX` / `sysconf()` / `pathconf()` |
| `include/limits.h` | `PATH_MAX` (256) |
| `posix/sys.c` | `times()` / `sysconf()` / `pathconf()`。`realpath()` が呼び手の器を越えて書かないようにした |

## 1. 立場

**この世代から作られる記録対象の成果物はまだ無い。** したがって本書に
SHA-256 の表は無い ([artifacts.md](../docs/artifacts.md))。

**カーネルの第 27 世代が要る。** `times()` が `times` (153) を呼ぶので、
`kernel26` 以前では `ENOSYS` になる。

## 2. 止まっていた 2 単位

| 単位 | 使うもの | 止まっていた理由 |
|---|---|---|
| `libiberty/getruntime` | `<sys/times.h>` / `times()` / `sysconf(_SC_CLK_TCK)` | header が無い (`hdr`) |
| `libiberty/lrealpath` | `PATH_MAX` / `pathconf(_PC_PATH_MAX)` | 名前が無い (`decl`) |

`getruntime.c` は、host の configure が `HAVE_TIMES` を立てると
`<sys/times.h>` を無条件に含める。1 秒あたりの tick 数は `_SC_CLK_TCK` が
定義されていれば `sysconf()` で訊き、無ければ `HZ`、それも無ければ
`CLOCKS_PER_SEC` を使う。

**`CLOCKS_PER_SEC` は足していない。** これは `clock()` の単位であって
`times()` の単位ではない。POSIX (XSI) は `CLOCKS_PER_SEC` を 1000000 と
定めているので、`_SC_CLK_TCK` を配らずに `CLOCKS_PER_SEC` だけを置くと、
`getruntime` は 100 Hz の tick を 1 MHz として換算し、1 万倍小さい値を
返す。**通ったうえで値が違う**形になる。`sysconf(_SC_CLK_TCK)` を本当の
値で配るのが正しい経路である。

## 3. 足したもの

### `times()` —— 4 つの欄はどれも本当の値である

カーネル (`kernel27`) がトラップの出入りで goldfish RTC を読み、その時点で
走っているプロセスへ時間を足している。CPU は 1 つで、`spawn` は子が
終わるまで親を止めるので、**走っているのは常に最も深いプロセス**である。

| 欄 | 値 |
|---|---|
| `tms_utime` | 前回トラップを出てから次にトラップへ入るまでの時間の合計 |
| `tms_stime` | そのプロセスの syscall を処理していた時間の合計 |
| `tms_cutime` | 終わった子の `tms_utime + tms_cutime` の合計 |
| `tms_cstime` | 終わった子の `tms_stime + tms_cstime` の合計 |

単位は tick (10 ms) で、`sysconf(_SC_CLK_TCK)` は 100 を返す。100 は
Linux の USER_HZ と同じ値である。戻り値は起動からの tick 数で、差にだけ
意味がある (POSIX も「過去のある時点から」としか言っていない)。

**限界が 1 つある。** カーネルはタイマ割込みを使っていないので、syscall
を呼ばずに走り続けている間の `tms_utime` は次の syscall で足される。
`times()` 自身が syscall なので、呼んだ時点までの分は必ず入っている。

### `sysconf()` / `pathconf()` —— 答を持っている名前だけ

POSIX の約束では、名前が定義されていて `-1` が返れば「上限が無い」と
読まれる。持っていない上限をそう読ませるのは嘘になるので、**答を持って
いる名前だけを定義した**。知らない名前には `EINVAL` で `-1` を返す。

| 名前 | 値 | 根拠 |
|---|---|---|
| `_SC_CLK_TCK` | 100 | `kernel27` の tick は 10 ms |
| `_PC_PATH_MAX` | `PATH_MAX` (256) | 下記 |

番号は glibc と同じにしてある (`_SC_CLK_TCK` = 2、`_PC_PATH_MAX` = 4)。
`pathconf()` は経路が無ければ `stat()` と同じ errno で `-1` を返す。

### `PATH_MAX` = 256 (終端の NUL を含む)

`kernel27` の `stat` と `spawn` は経路を 255 バイト + NUL の器で受け、
それより長ければ `E2BIG` で拒む。**どの syscall にも渡せる長さ**の上限が
この値である。POSIX の最小値 `_POSIX_PATH_MAX` (256) を満たす。

`kernel26` 以前は写す側の容量が 1 バイト足りず、254 バイト + NUL までしか
通らなかった。`kernel27` で定義どおりに直した (`kernel27.c` の註)。

### `realpath()` は呼び手の器を越えて書かない

`PATH_MAX` を定義すると、`lrealpath` は `char buf[PATH_MAX]` を用意して
`realpath (filename, buf)` を呼ぶ。第 24 世代までの `realpath()` は内部の
1024 バイトまで結果を書いていたので、**256 バイトを超える結果は呼び手の
器を越えて書かれる**。第 25 世代では、呼び手が器を渡したときは `PATH_MAX`
バイトを上限とし、越える結果は書かずに `ENAMETOOLONG` を返す。器を渡さない
(NULL の) ときは従来どおり 1024 バイトまで扱う。

## 4. 測り方

`tools/diff17.sh` の OS 側に `timesx` を足した (`tests/stage017/probe`)。
値そのものは実行ごとに違うので、**欄が満たすべき性質**をホストと
突き合わせる。

| 見るもの | 0 を返すだけの `times` だとどうなるか |
|---|---|
| `sysconf(_SC_CLK_TCK)` が 100 | 通る |
| 知らない名前は `EINVAL` で `-1` | 通る |
| `PATH_MAX` と `pathconf("/", _PC_PATH_MAX)` が 256 以上 | 通る |
| 計算を続ければ CPU 時間が 5 tick 以上増える | **破れる** |
| 時間が戻らない | 通る |
| 子を待っていなければ `cutime` / `cstime` は 0 | 通る |

**無い経路を `pathconf()` に訊いたときは突き合わせない。** 最初は
「`ENOENT` で `-1`」を性質として並べたが、ホスト (glibc) は経路を見ずに
上限を返し、そこで食い違った。POSIX は `ENOENT` を「失敗してよい」としか
言っていないので、どちらも誤りではない。**両方が正しくありうる性質は
突き合わせの対象にならない。** 我々が `ENOENT` を返すことは、ホストと
比べずに OS の上の検査 (`tests/stage017/user/tmx.c`) で見る。

ホストに無いもの (`spawn` した子の時間と、255 バイトの経路が通ること)
も同じ検査で見る (tests/stage017 第 9 部)。

## 5. ビルド

```
sh tools/build.sh stage017
# cc15af + pp で l25_*.o を組む
```

最前線の `cc15af` で組む。`libc24` は `cc15ac` で組んでいたが、
**最前線を 2 つに分けない**。
