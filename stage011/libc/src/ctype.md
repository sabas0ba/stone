# ctype.o 説明文書 (第 11 世代)

ctype.o はフリースタンディング libc の一部で，文字の分類と変換
(C89 7.3) を収める。設計は
[stage011-libc.md](../docs/stage011-libc.md) 3.3。

ソース [ctype.c](ctype.c) とヘッダ
[../include/ctype.h](../include/ctype.h) が正本である。
オブジェクトはビルドで再現される生成物であり，git 管理しない。

## ビルド

Stage 10 の成果物 (pp + cc) でビルドする。

```
sh tools/build.sh stage011
# bundle(ctype.h ctype.c) | pp | cc -> ctype.o
```

SHA-256: 58ad6ed541e2ba02a6801bb5b78ca2e3bcdd3b091ade6b291c8a00ae94a1392a

- 形式: ELF リロケータブルオブジェクト (RV32)，3984 バイト
- コンパイラ: cc10l ([../stage010/cc12.md](../stage010/cc12.md))

## 収録する関数

isalnum, isalpha, iscntrl, isdigit, isgraph, islower, isprint, ispunct,
isspace, isupper, isxdigit, tolower, toupper

表ではなく範囲比較で実装する。EOF (-1) を渡しても偽を返す。
分類は ASCII に固定である (ロケールを持たない)。
