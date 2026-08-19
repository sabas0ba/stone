# cc15p 説明文書

cc15p は C コンパイラの Stage 15 第 6 部の世代である。
[cc15o.sc](cc15o.sc) を出発点に，**構造体の配置を C89 (と ilp32 の
慣行) に揃えた**。黙って誤る (bad) 誤りが 2 つ直っている。

ソース [cc15p.sc](cc15p.sc) が正本である。

## ビルド

```
sh tools/build.sh stage015
# cc15o(cc15p.sc) -> cc15p0     (1 段目)
# cc15p0(cc15p.sc) -> cc15p     (正本。以降は固定点)
```

SHA-256: 1953eddbb26944580286e53bee7424591b6ecffe9553fb7d60e866fa1178aa79

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 直したもの (bad)

正解は `riscv32-tcc` に聞いた (第 5 部で我々が backend を書いたもの。
ilp32 の慣行は gcc と同じ)。

| 検査 | cc15o | cc15p / 正 |
|---|---|---|
| `sizeof` (`unsigned short` のビットフィールド 16 bit) | 4 | **2** |
| `sizeof` (`unsigned char` のビットフィールド 8 bit) | 4 | **1** |
| `sizeof` (`unsigned char` のビットフィールド 9 bit) | 4 | **2** |
| `sizeof(struct { char c; })` | 4 | **1** |
| `sizeof(struct { int; long long; })` | 12 | **16** |
| `long long` / `double` メンバの位置 | 4 | **8** |
| `sizeof(struct { char; struct { char; short; }; })` | 8 | **6** |

**1. ビットフィールドの記憶単位は宣言された型である。** cc15o までは
一律 `unsigned` (4 バイト) で，`unsigned short` / `unsigned char` の
宣言を無視していた。幅がその単位に収まらなければ次の単位へ送る。

**2. `long long` / `double` の整列は 8 である。** cc15o までは 4 だった。

あわせて，構造体の大きさを「常に 4 の倍数」から「**自身の整列の倍数**」
へ直した。構造体ごとの整列は `salign` に持ち，メンバの整列の最大を取る。
無名メンバの展開 (`msplice`) も内側の整列で置く。

## 波及: 「構造体の大きさは 4 の倍数」が崩れる

`scopy` (構造体の複写) は語ごとに写していた。大きさが 4 の倍数で
なくなると**隣を壊す**ので，語で写せるところまで写し，端数を半語・
バイトで始末するようにした。

実引数と返却は `ceil(大きさ/4)` 語のままである。こちらは**読み過ぎる**
だけで，書き先はその語数で確保してあるため害が無い。

## 検査

- 固定点: cc15p が自分自身を翻訳した結果が 2 回目と一致する
- 配置: [layout](../tests/stage015/probe/layout.c) の 16 項目。
  期待値は `riscv32-tcc` に静的表明で確認したものである
- 回帰: sh / ed / mk・rt64 / rtfp・kernel15 / kernel16・libc15 の
  8 ファイル・pp15 / pp16 / ld15 / ld16 が cc15o と**バイト一致する**
  `.o` になる (我々のソースに `long long` / `double` の構造体メンバも
  short / char のビットフィールドも無いことの裏付けでもある)
- [gap15m](../tests/stage015/probe/gap15m.c) の検査 `r` の期待値を
  12 から 16 へ直した。**古い期待値のほうが誤った配置を写していた**
  (無名共用体が `unsigned long long` を含むので整列 8 になる)
