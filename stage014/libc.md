# libc 第 14 世代 説明文書

[stage013/libc](../stage013/libc) を全文複製し，外部の C が前提にする
不足を埋めた ([artifacts.md](../docs/artifacts.md) の凍結の作法)。
設計は [stage014-external.md](../docs/stage014-external.md) 10 章。

## ビルド

```
sh tools/build.sh stage014
# bundle(include/*.h <src>) | pp | cc14e -> l14_*.o
```

**最前線の cc14e でコンパイルする** (l13 までは cc10l)。外部ソースと
同じ経路に載せるためである。生成物は `l14_` を前置して呼ぶ。

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

## 検証

[../tests/stage014/test.sh](../tests/stage014/test.sh) の
「libc 第 14 世代」節。OS (kernel13) の上で lib14 (printf 拡張・
sprintf・assert 成立) と abrt (assert 失敗が式の文字列を出して
exit(1)) を走らせ，出力と終了コードを照合する。
