# morecore.o 説明文書 (第 14 世代)

morecore.o は malloc の供給源 (固定領域版) である。ヒープを `.bss` 上の
1 MiB に置く。設計は [stage011-libc.md](../docs/stage011-libc.md) 7.1。

**stdlib.o と対で使う。** 供給源を 1 つの翻訳単位に切り出してあるので，
環境に応じて差し替えられる ([stage012-os.md](../docs/stage012-os.md) 6.2)。

| 環境 | 並べるもの |
|---|---|
| ベアメタル | `stdlib.o` + `morecore.o` (本ファイル) |
| 自作 OS の上 | `stdlib.o` + `p_morecore.o` (brk 版) + `p_sys.o` |

ソース [morecore.c](morecore.c) が正本である。

## ビルド

```
sh tools/build.sh stage014
# bundle(stddef.h morecore.c) | pp | cc -> morecore.o
```

SHA-256: 700cd7df10e7960b5ec0c9102f8bead714655718d7a6a08444855c2ed7611fd3

- 形式: ELF リロケータブルオブジェクト (RV32)，1100 バイト
- コンパイラ: cc10l ([../stage010/cc12.md](../stage010/cc12.md))

## 提供する関数

```c
void *morecore(size_t nu, size_t *got);
```

nu 単位 (1 単位 = 8 バイト) 以上の領域を確保して先頭を返し，実際に確保した
単位数を `*got` へ返す。失敗なら NULL。最小の要求量は 512 単位で，残量が
足りなければ要求量ちょうどまで縮めて試みる。
