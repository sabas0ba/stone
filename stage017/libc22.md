# libc22 --- `putc` を足した世代 (第 22 世代)

`libc21` の全文複製に **1 行**足した世代である。差は
`include/stdio.h` の `putc` だけで、`.c` は 1 バイトも変えていない。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.3 の 8。

## 1. 立場

`libc21` と同じく、**この世代から作られる記録対象の成果物はまだ無い**
([libc21.md](libc21.md) 1 節)。鎖の側 (`cc19` が tcc を組む道) は
`libc20` のままである。したがって本書にも SHA-256 の表は無い。

ここで足したものは、**GCC 4.7.4 のソースを読んで判った我々の libc の
穴**である。`libcpp` の `lex` / `line-map` / `mkdeps` の 3 単位が
これ 1 つで止まっていた。

## 2. `libc21` との差

C89 7.9.7.8 ——

> `int putc(int c, FILE *stream);`
>
> `putc` 関数は `fputc` と等価であるが、マクロとして実装される場合、
> `stream` を副作用を持つ式で評価してよい。

我々の `stdio.h` は `fputc` と `putchar` を持ちながら `putc` を
持っていなかった。

```c
#define putc(c, f) fputc((c), (f))
```

## 3. なぜ関数ではなくマクロか

**関数として宣言しても効かない。**

```c
typedef struct f FILE;
int putc(int c, FILE *s);
int main(void) { return putc(65, 0); }     /* cc は 5 (引数個数の不一致) */
```

**鎖の前置部が 1 引数の `putc` を primitive として持っている**
([stage015/cc15ab.sc](../stage015/cc15ab.sc) の `bireg()`)。器の組込みが
勝つので、header 側の宣言は届かない。

前置部の名前を替える道もあるが、**鎖の成果物がすべて変わる** ——
`putc` は `getc` / `exit` と並ぶ 3 つの primitive の 1 つで、Stage 1 から
すべての `.o` がこの名前で未定義シンボルを持っている。そこには触らない。

**C89 はマクロを明示的に許している。** 7.9.1 ——

> この見出しで宣言する関数は、マクロとしても追加で実装してよい。

したがって `#define putc(c, f) fputc((c), (f))` は規格に沿った実装で
ある。`pp` の段で書き換わるので、器には一切触れずに済む。

## 4. 限界: 関数としては使えない

C89 7.9.1 は続けて、マクロを抑止して関数を呼ぶ道を認めている。

```c
(putc)(c, f)      /* マクロは展開されない */
&putc             /* 関数へのポインタ */
```

**どちらも我々では 5 で落ちる。** マクロが展開されないので前置部の
1 引数 `putc` に当たる。

GCC 4.7.4 の libiberty / libcpp はどちらの形も使っていない —— すべて
呼出しの形である。**会ってから足す**。足すときは前置部の側を触ることに
なるので、鎖の成果物がすべて変わる覚悟が要る。

## 5. 鎖は変わらない

**`.o` 11 本が `libc21` のものとバイト一致する。**

```
l22_src_string.o  l22_src_ctype.o   l22_src_stdlib.o
l22_src_morecore.o l22_src_misc15.o
l22_posix_sys.o   l22_posix_morecore.o l22_posix_stdio.o
l22_posix_assert.o l22_posix_dir.o  l22_posix_signal.o
```

我々自身のソースは `putc` を 1 度も呼んでいない (`putchar` と `fputc`
だけ) ので、マクロを置いても展開される場所が無い。
`tests/stage017/test.sh` がこの一致を見る。

## 6. 測り方

- `tests/stage017/test.sh` 第 7 部が、**我々の OS の上で**
  `putc(c, stdout)` を使うプログラムを `cc19` に訳させ、走らせて
  出力を見る。header を足しただけでは「訳せた」までしか言えない ——
  `fputc` へ書き換わった先が本当に書けることを見る
- 同じ第 7 部が `.o` 11 本のバイト一致を見る (5 節)
- `tools/gcc17.sh` の既定の libc をこの世代にした。`lex` / `line-map` /
  `mkdeps` の 3 単位が `.o` まで通る (8.2)
