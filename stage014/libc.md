# libc 第 14 世代 説明文書

[stage013/libc](../stage013/libc) を全文複製し，外部の C が前提にする
不足を埋めた ([artifacts.md](../docs/artifacts.md) の凍結の作法)。
設計は [stage014-external.md](../docs/stage014-external.md) 10 章。

## ビルド

```
sh tools/build.sh stage014
# bundle(include/*.h <src>) | pp | cc14f -> l14_*.o
```

**最前線の cc でコンパイルする** (l13 までは cc10l。第 8 部からは cc14f)。
外部ソースと同じ経路に載せるためである。生成物は `l14_` を前置して呼ぶ。
cc14f は 2048 バイト未満のフレームの関数に cc14e と同一のコードを出すので，
第 7 部当時とオブジェクトは変わらない。

## 第 13 世代からの差分

### assert.h (新規)

```c
assert(式);          /* 不成立なら式の文字列を stderr へ出して exit(1) */
```

- `NDEBUG` が定義されていれば何もしない式になる
- C89 の `abort` (シグナル) は無いので `exit(1)` で表す
- 実体は `posix/assert.c` の `__assert`

### stdio (printf の拡張と sprintf 系)

- **`l` 修飾** (`%ld` `%lu` `%lx`): `long` は `int` と同じ幅なので
  読み捨てる
- **`-` (左詰め)**: 数値と `%s` の両方。`0` と併用したら `-` が勝つ
- **`%s` の幅**: 右詰め (既定) と左詰め
- **`sprintf` / `vsprintf` / `vfprintf`**: 書式化の書込み先を FILE か
  緩衝かで切り替える 1 点 (`emitc`) を入れた。返り値は書いた長さ
- `pnum` の返り値 (桁数の数え) が詰め物しか数えていなかったのを直した。
  printf の返り値が正しい長さになる

純粋部 (string / ctype / stdlib) と環境部の sys / morecore は
第 13 世代から変えていない (オブジェクトはコンパイラが変わるため
バイト一致はしない)。

## 制限

### `fopen(path, "a")` は追記にならない

`"a"` は `O_WRONLY | O_CREAT` で開くだけで，`O_APPEND` も末尾への
seek も無い (`posix/stdio.c` の `fopen`)。したがって**既存ファイルの
先頭から上書きする**。`"w"` との違いは `O_TRUNC` を付けないことだけで，
元の内容より短く書けば後ろに古い内容が残る。

追記が要るなら `fopen` の前に長さを調べて自前で位置を進める，という
逃げ道も無い——**seek 系の syscall がそもそも無い** (
[stage012-os.md](../docs/stage012-os.md) 5.4)。

kernel14 で sfs の上書きが隣接ファイルを壊さなくなった
([stage014-external.md](../docs/stage014-external.md) 13.1) が，これは
別の話である。上書きが安全になっただけで，`"a"` が追記になるわけでは
ない。`"a"` を C89 どおりにするには `lseek` (または `O_APPEND`) を
カーネルへ足し，libc 側で使う必要がある。**現状は「`"a"` は非対応」と
決め，使わない。**

## 検証

[../tests/stage014/test.sh](../tests/stage014/test.sh) の
「libc 第 14 世代」節。OS (kernel13) の上で lib14 (printf 拡張・
sprintf・assert 成立) と abrt (assert 失敗が式の文字列を出して
exit(1)) を走らせ，出力と終了コードを照合する。
