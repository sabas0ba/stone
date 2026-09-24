# cc15ae --- C コンパイラ 第 15 世代 その 31

`cc15ad` との差は 1 か所 2 行 (註を除く)。ソースは `stage015/cc15ae.sc`。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.3 の 14。

## 直したもの: 仮引数の `register`

C89 6.5.1 ——

> 仮引数の宣言に書ける記憶域クラス指定子は `register` だけである。

`libiberty/hashtab.c` の `iterative_hash` がこの形で書かれている。

```c
hashval_t iterative_hash (const void *k_in,
                          register size_t length,
                          register hashval_t initval)
```

`pparam()` は `ptype()` を直に呼んでいて、その手前で記憶域クラスを
読み捨てていなかった。**`plocal()` と `topdecl()` は前からしている** ——
仮引数並びの側だけが持っていなかった。

K&R 形式の宣言 (`krdecl()`) も同じ仮引数の宣言なので、そちらにも
同じ読み捨てを置いた。

| 書き方 | `cc15ad` | `cc15ae` |
|---|---|---|
| `int f(int x)` | 通る | 通る |
| `int f(register int x)` | **1 で拒む** | 通る |
| `int f(register const unsigned char *k)` | **1 で拒む** | 通る |
| `int f(a, b) register int a; int b; { }` (K&R) | **1 で拒む** | 通る |
| `int main(void){ register int x; }` (局所) | 通る | 通る |

## これは `cc15ad` の直しが足りなかったのではない

**`libiberty/hashtab` には未対応箇所が 2 つあった。**

| `.i` の行 | 形 | 通した世代 |
|---|---|---|
| 1691 | `size_t (htab_size) (htab_t htab)` | `cc15ad` (8.3 の 3) |
| 2257 | `register size_t length` | `cc15ae` (8.3 の 14) |

`cc15ad` を測ったとき、5 単位が動くと予測して 4 単位しか動かなかった。
**予測と異なった理由を確かめずに「修正が有効でなかった」と書かないこと** ——
`.i` の行番号を両方見て、1 つめが確かに通っていることを確認した。

8.2 が繰り返し書いている「**1 つ通すと次が見える**」が、ここでも出た。

## ビルドチェーンは変わらない

既存のソースは仮引数に `register` を書いていない。`sh` / `ed` / `mk` を
訳した `.o` は `cc10l` のものと 1 バイトも変わらない。

## 測り方

`tests/stage015/probe/c89blk.c` に足した。**`register` は割付けを変え
ないので、値が変わってはいけない** —— 変われば我々の側が誤っている。

```c
static int mix(register int a, register unsigned b, int c) { ... }
static int krmix(a, b) register int a; int b; { ... }
```

`cc15ad` で同じプローブを走らせると「我々だけが拒む」で落ちる
(`STONE_DIFF_CC=tmp/build/cc15ad.bin`)。

## ビルド

```
sh tools/build.sh stage015
# cc15ad(cc15ae.sc) -> cc15ae0    (1 段目)
# cc15ae0(cc15ae.sc) -> cc15ae    (2 段目。以降は固定点)
```

SHA-256: 92816efc0d73fc8c0a1044a29812963f887d0e5ca39127961db4544854f334fb

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
