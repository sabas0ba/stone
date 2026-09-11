# cc15z --- C コンパイラ 第 15 世代 その 26

`cc15y` との差は `fnpdec1()` の 1 か所 1 行 (註を除く)。ソースは
`stage015/cc15z.sc`。経緯は
[docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.3 の 11。

## 直したもの: 括弧の中の宣言子の型修飾子

C89 6.5.4.1 ——

> pointer:
>   `*` type-qualifier-list_opt

なので `int (*const f)(int)` は妥当な宣言子である。

**括弧の外では前から通っていた。** `int *const p;` は `pstars()` が
`*` の後ろの `const` / `volatile` を読み飛ばしている
(`stage015/cc15z.sc` 2085 行)。

```c
  while (tok == o_mul || tok == k_const || tok == k_volatile) {
    if (tok == o_mul) b = b + 65536;
    next();
  }
```

**括弧の中の宣言子は `pstars` を通らない。** `fnpdec1()` は `(` `*` を
読んだ直後に名前か `)` を期待しており、`const` が来ると 1 (構文誤り) で
止まる。`while` 1 行を足して揃えた。

| 書き方 | `cc15y` | `cc15z` |
|---|---|---|
| `int *const p;` (括弧の外) | 通る | 通る |
| `int g(int (*const f)(int))` (定義) | **1 で拒む** | 通る |
| `int g(int (*const)(int));` (原型) | **1 で拒む** | 通る |
| `int (*const f)(int) = 0;` (局所) | **1 で拒む** | 通る |
| `int g(int (*volatile f)(int))` | **1 で拒む** | 通る |

**これで括弧の中の宣言子を読む道が 3 世代そろった** ——
[cc15w](cc15w.md) が名前の省略 (仮引数並び)、[cc15x](cc15x.md) が
cast の型名、cc15z が型修飾子である。**どれも「括弧の外では既に
できていたことが、括弧の中だけ抜けていた」**という同じ形の抜けだった。

## 表に出た形

GCC 4.7.4 の `libcpp/charset.c` 455 行 ——

```c
static unsigned char
conversion_loop (int (*const one_conversion)(iconv_t, const uchar **, size_t *,
					     uchar **, size_t *),
		 iconv_t cd, const uchar *from, size_t flen,
		 struct _cpp_strbuf *to)
```

`libcpp/charset` 1 単位が止まっていた。

## 鎖は変わらない

既存のソースにこの形は無い。`sh` / `ed` / `mk` を訳した `.o` は
`cc10l` のものと 1 バイトも変わらず、`cc15z0` と `cc15z` もバイト一致
する (`tests/stage015/test.sh` が両方を見る)。

## 測り方

- 台帳 `tests/stage015/ledger.txt` に `fnpqual` を `ok y\n` で足した。
  本処理系は修飾子を検査に使わないので読み飛ばすだけでよいが、
  **読み飛ばした先が正しく繋がることは見る** —— `charset.c` 455 行と
  同じ形 (`const` 付きの関数ポインタを先頭に置き、後ろにスカラを並べる)
  で実際に呼び、仮引数の位置がずれていないことを確かめる。原型・定義・
  局所の宣言・`volatile`・括弧の外の `const` をすべて通す
- `tools/diff17.sh` がホストの `gcc` と突き合わせる。ホストも
  `-std=c89 -pedantic-errors` で通し、同じ `y` を出す

## 世代名について

**`cc15z` で 1 文字が尽きた。** 次からは `cc15aa` / `cc15ab` … と 2 文字へ
延ばす (持ち主の判断)。`cc15a..cc15z` の並びをそのまま延長する形なので、
既存の `.md` / ビルド / 検査の参照形式は一切変わらない。**辞書順と世代順は
一致しなくなる** (`cc15aa` は `cc15a` の直後に並ぶ) ので、並べるときは
この `.md` の「第 15 世代 その N」を見る。

## ビルド

```
sh tools/build.sh stage015
# cc15y(cc15z.sc) -> cc15z0     (1 段目)
# cc15z0(cc15z.sc) -> cc15z     (正本。以降は固定点)
```

SHA-256: 51b034da8441a8babeb1b11fae776e09bebdc4f368955e855243d9ba2750c499

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
