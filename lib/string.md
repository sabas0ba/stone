# string.o 説明文書

string.o はフリースタンディング libc の一部で，文字列と記憶域の操作
(C89 7.11) を収める。設計は
[stage011-libc.md](../docs/stage011-libc.md) 3.2。

ソース [string.c](string.c) とヘッダ
[../include/string.h](../include/string.h) /
[../include/stddef.h](../include/stddef.h) が正本である。
オブジェクトはビルドで再現される生成物であり，git 管理しない。

## ビルド

Stage 10 の成果物 (pp + cc) でビルドする。リンク済みの実行像ではなく
オブジェクトのまま置き，利用者が必要なものだけ ld へ並べる。

```
sh tools/build.sh stage011
# bundle(stddef.h string.h string.c) | pp | cc -> string.o
```

SHA-256: 71ad0ad02741a4e874492ce48ee08def32f4ffadd4a051523fa73c35d767d11b

- 形式: ELF リロケータブルオブジェクト (RV32)，7156 バイト
- コンパイラ: cc10l ([../stage010/cc12.md](../stage010/cc12.md))

## 収録する関数

| 群 | 関数 |
|---|---|
| 記憶域 | memcpy, memmove, memset, memcmp |
| 複写・連結 | strcpy, strncpy, strcat, strncat |
| 比較 | strcmp, strncmp |
| 探索 | strlen, strchr, strrchr, strstr, strspn, strcspn, strpbrk |

比較はすべて unsigned char として行う (C89 の要求)。strtok (状態を持つ),
memchr, strcoll / strxfrm / strerror は第 1 部の範囲外である
([stage011-libc.md](../docs/stage011-libc.md) 1.3)。
