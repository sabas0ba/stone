# cc15ar --- C コンパイラ 第 15 世代 その 44

`cc15aq` との差は，ポインタの大小比較を符号なしで行うことである (註を除く)。ソースは `stage015/cc15ar.sc`。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.15。

stone の OS の上で走らせた GCC の cc1 の `.s` を，host で組んだ同じ cc1 の `.s` と突き合わせて見つけた。

## 直したもの: ポインタの大小比較

ポインタの大小 (C89 6.3.8) は番地の順である。`cc15aq` までは整数と同じく符号つきで比べていた (`slt`)。我々の RAM は 0x8000_0000 から上にあるので，符号つきで見ると番地はすべて負であり，NULL より小さい。

2 つの番地がどちらも RAM の中なら符号が揃うので答は合う。答が逆になるのは NULL や 0x7fff_ffff 以下の値との比較である。

GCC の `default_elf_asm_output_ascii` (varasm.c) は

<!--
SPDX-SnippetBegin
SPDX-SnippetCopyrightText: Free Software Foundation, Inc.
SPDX-License-Identifier: GPL-3.0-or-later
-->
```c
const char *last_null = NULL;
...
if (s > last_null)
```

<!-- SPDX-SnippetEnd -->

で分岐する。`s > NULL` が偽になり，`.eh_frame` の空の文字列を `.string ""` ではなく `.ascii "\000"` で出していた。アセンブラが作るバイト列は同じなので，`.s` を突き合わせるまで表に出なかった。

どちらかの被演算子がポインタなら，`sltu` の系統 (`b_ult` / `b_ugt` / `b_ule` / `b_uge`) で比べる。等値比較 (`==` / `!=`) は符号に依らないので変えていない。

## ビルドチェーンは変わらない

`sh` / `ed` / `mk` を訳した `.o` は `cc10l` のものと 1 バイトも変わらない。

## 測り方

`tests/stage015/probe/ptrcmp.c` は NULL との `<` `<=` `>` `>=`，配列の中の 2 つの番地，整数から作った 0x8000_000f，`?:` と `if` と `while` の中の比較を見る。

| | `cc15aq` | `cc15ar` |
|---|---|---|
| `tools/diff17.sh ptrcmp` | **値が違う** (NULL との比較が逆) | 値が一致 |
| cc1 の `.s` と host の cc1 の `.s` | `.eh_frame` の 1 行が違う | (8.15 の測定) |

## ビルド

```
sh tools/build.sh stage015
# cc15aq(cc15ar.sc) -> cc15ar0    (1 段目)
# cc15ar0(cc15ar.sc) -> cc15ar    (2 段目。以降は固定点)
```

SHA-256: 2966bb85fe585e248de228aa868e980cfff319cd0351d202bfc018ca312a12a5

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
