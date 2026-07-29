# stdlib.o 説明文書

stdlib.o はフリースタンディング libc の一部で，記憶域の管理
(C89 7.10.3) を収める。設計は
[stage011-libc.md](../docs/stage011-libc.md) 7 章。

ソース [stdlib.c](stdlib.c) とヘッダ
[../include/stdlib.h](../include/stdlib.h) /
[../include/stddef.h](../include/stddef.h) が正本である。
オブジェクトはビルドで再現される生成物であり，git 管理しない。

## ビルド

Stage 10 の成果物 (pp + cc) でビルドする。

```
sh tools/build.sh stage011
# bundle(stddef.h stdlib.h stdlib.c) | pp | cc -> stdlib.o
```

SHA-256: 809ec568887b13fb9af27799a01230547948170097396993f56f0b3f9144901b

- 形式: ELF リロケータブルオブジェクト (RV32)，5008 バイト
- コンパイラ: cc10l ([../stage010/cc12.md](../stage010/cc12.md))

## 収録する関数

malloc, free, calloc, realloc

割付けは K&R 型のフリーリスト first-fit で，ヒープは `.bss` 上の
固定領域 1 MiB である。領域の供給は内部関数 morecore に集約してあり，
Stage 12 で brk へ差し替える ([stage011-libc.md](../docs/stage011-libc.md)
7.1)。整列は 4 バイト。qsort / bsearch / atoi / strtol などは
第 3 部，exit / abort / atexit は Stage 12 の範囲である。
