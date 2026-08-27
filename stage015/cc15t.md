# cc15t --- C コンパイラ 第 15 世代 その 20

`cc15s` との差は `ptype()` の 1 か所 1 行 (註を除く)。ソースは
`stage015/cc15t.sc`。経緯は
[docs/stage017-gcc.md](../docs/stage017-gcc.md) 5.1。

## 直したもの: 型修飾子が宣言指定子の列の途中に来る形

```c
unsigned const char *scan;
```

を**拒んでいた**。C89 6.5 の宣言指定子は

```
declaration-specifiers:
    storage-class-specifier declaration-specifiers(opt)
    type-specifier          declaration-specifiers(opt)
    type-qualifier          declaration-specifiers(opt)
```

で，並びに順序の制約が無い。`const unsigned char` も
`unsigned const char` も `unsigned char const` も同じ型である。

`ptype()` は `const` / `volatile` を**列の先頭**と**列の末尾**の 2 か所
でしか読んでいなかった。整数型指定子 (`unsigned` / `signed` / `short` /
`long` / `int` / `char`) を集める環に修飾子の枝が無いので，
`unsigned const char` は `const` のところで環を抜ける。抜けた時点で
`uns = 1`・`bas = -1` なので `t_uint` が返り，末尾の環が `const` を
食べ，残った `char` を宣言子として読もうとして拒む。

`k_register` / `k_auto` (記憶域指定子) は既にこの環の中で読み捨てて
いた。**同じ場所に修飾子の枝が要る**というだけの話である。

```c
      else if (tok == k_register || tok == k_auto) { next(); }
      else if (tok == k_const || tok == k_volatile) { next(); }   /* 足した */
      else if (tok == k_int) { bas = 1; next(); }
```

## 見つかり方: zlib を我々の器で訳した

[docs/stage017-gcc.md](../docs/stage017-gcc.md) 5.1 の測定である。
`zlib` 14 単位 + `bzip2` 8 単位を，**我々の OS の上で我々の cc19 +
cc15s に**訳させた。22 単位のうち通らなかったのは 1 つ ——
`zlib/gzread.c` だけだった。

一構文ずつのプローブに落として絞ると，落ちるのは

```
unsigned const char *s   -> 拒む
const unsigned char *s   -> 通る
```

の差だった。**tcc を読ませて出た 4 つ (25 / 28 / 31 / 33 章) と同じ
形の収穫**である —— 外のソースは我々の C 適合を測る物差しであって，
出てくるのは我々の側の直しだけである。

## 鎖は変わらない

- `sh` / `ed` / `mk` を訳した `.o` は `cc10l` のものと 1 バイトも
  変わらない (`tests/stage015` の regress)
- `cc15t0` (= `cc15s` が `cc15t.sc` を訳したもの) と `cc15t`
  (= `cc15t0` が自分を訳したもの) が**バイト一致**する。足した枝は
  自分自身のソースには現れないためである

## 測り方

台帳 `tests/stage015/ledger.txt` の `qualorder`。
プローブ `tests/stage015/probe/qualorder.c` は 1 つの形ではなく
**列の作り方を並べて**見る ——

- 大域 / 局所 / 引数 / 戻り値 / 構造体メンバの各位置
- `unsigned const char` / `const unsigned char` / `unsigned char const`
- `volatile unsigned int` / `unsigned volatile int`
- `short const int` / `const short` / `unsigned const long` /
  `long const int` / `long const long`
- `sizeof` の値と，型変換 `(unsigned const char)300 == 44` /
  `(signed const char)200 == -56`

**符号と幅を必ず一緒に見る。** 修飾子を読み飛ばすときに `uns` を
落とすと `200` が `-56` になるが，拒まないので台帳には `ok` として
載ってしまう —— `bad` を作る道である。

## ビルド

```
sh tools/build.sh stage015
# cc15s(cc15t.sc) -> cc15t0     (1 段目)
# cc15t0(cc15t.sc) -> cc15t     (正本。以降は固定点)
```

SHA-256: 19ae9ba51b82cfc0b556452eb86021c609fd35636010c1883eb82518b4ed9d2a

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
