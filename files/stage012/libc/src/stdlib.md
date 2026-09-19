# stdlib.o 説明文書 (第 12 世代)

stdlib.o はフリースタンディング libc の一部で，記憶域の管理・整列と探索・
数値変換 (C89 7.10) を収める。設計は
[stage011-libc.md](../docs/stage011-libc.md) 7 章 (記憶域) と 8 章
(整列と探索・数値変換)。

ソース [stdlib.c](stdlib.c) とヘッダ
[../include/stdlib.h](../include/stdlib.h) /
[../include/stddef.h](../include/stddef.h) が正本である。
オブジェクトはビルドで再現される生成物であり，git 管理しない。

## ビルド

Stage 10 の成果物 (pp + cc) でビルドする。

```
sh tools/build.sh stage012
# bundle(stddef.h stdlib.h stdlib.c) | pp | cc -> stdlib.o
```

SHA-256: 74d02bcdd54bb606c56a38a1c2ae0be25c01f53fa292dc673a3dd43126e7e225

- 形式: ELF リロケータブルオブジェクト (RV32)，9696 バイト
- コンパイラ: cc10l ([../stage010/cc12.md](../stage010/cc12.md))

## 収録する関数

| 群 | 関数 |
|---|---|
| 記憶域 | malloc, free, calloc, realloc |
| 整列と探索 | qsort, bsearch |
| 数値変換 | atoi, strtol, abs, div |

割付けは K&R 型のフリーリスト first-fit で，ヒープは `.bss` 上の
固定領域 1 MiB である。領域の供給は内部関数 morecore に集約してあり，
Stage 12 で brk へ差し替える ([stage011-libc.md](../docs/stage011-libc.md)
7.1)。整列は 4 バイト。

qsort の実装は Shell ソート (非再帰・追加記憶域なし。8.1)。strtol の
溢れは LONG_MAX / LONG_MIN への飽和のみで表す (errno は Stage 12 まで
無い)。atol / labs / ldiv (long == int の別名)・strtoul・rand は非目標，
exit / abort / atexit は Stage 12 の範囲である。
