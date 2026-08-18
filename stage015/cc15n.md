# cc15n 説明文書

cc15n は C コンパイラの Stage 15 第 6 部の世代である。
[cc15m.sc](cc15m.sc) を出発点に，**整数定数の型**を C89 のとおりにした。
黙って値を変える (bad) 誤りが 1 つ直っている。

ソース [cc15n.sc](cc15n.sc) が正本である。

## ビルド

```
sh tools/build.sh stage015
# cc15m(cc15n.sc) -> cc15n0     (1 段目)
# cc15n0(cc15n.sc) -> cc15n     (正本。以降は固定点)
```

SHA-256: 5e0c2e7d0c86e2b3e098ab936c454ee45b8e337aaba438b1277b98dd728399af

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 直したもの (bad)

C89 6.1.3.2 は，整数定数の型を「値が収まる**最初のもの**」と定める
(int → unsigned int → long → unsigned long)。`0x80000000` は
signed int に収まらないので **unsigned int** である。

cc15m まではこれを一律に int として扱っていた。32 bit の世界に閉じて
いる限り bit の並びは同じなので表に出ないが，**64 bit へ広げるときに
符号拡張と零拡張の差が出る**。

```c
unsigned long long l = 0xFFFFFFFFFFFFFFFFULL;
l & 0x80000000        /* 正しくは 0x0000000080000000 */
                      /* cc15m は 0xFFFFFFFF80000000 にしていた */
```

これは tcc が「32 bit の定数を 64 bit の欄へ符号拡張する」定石

```c
return (uint32_t)l1 | -(l1 & 0x80000000);      /* tccgen.c の value64 */
```

を壊す。第 6 部で我々の鎖が作った tcc (T1) は，この式が誤るために
**定数 -1 を 0x00000000FFFFFFFF として持ち**，即値を使う判定
(`fc == vtop->c.i`) が負の定数で必ず外れていた。結果として T1 の出力は
ホストの tcc より 2 割小さく，`c == -1` のような式で余計な命令を出して
いた ([stage015-tcc.md](../docs/stage015-tcc.md) 12.14)。

## 入れたもの

- 字句解析に `tvalu` (その定数の型が符号なしか) を足した
  - `U` 接尾辞 (cc15m までは読み捨てていた)
  - 32 bit に収まるが signed int に収まらない値 (bit 31 が立つ)
  - 64 bit のリテラルで signed long long に収まらない値 (bit 63)
- 一次式で型に反映する (`t_uint` / `t_ullong`)

`unsigned` になった定数を 64 bit へ広げる経路 (`ext32`) は元から
符号を見ているので，型さえ正しく付けば零拡張になる。

## 検査

- 固定点: cc15n(cc15n.sc) == cc15n0(cc15n.sc) (B2 == B3)
- 回帰: sh / ed / mk と rt64.c / rtfp.c の `.o` が**変わらない**。
  これらは 32 bit に閉じた使い方しかしていないので，型が変わっても
  生成コードは同じである
- [litu](../tests/stage015/probe/litu.c): 定数の型が絡む 7 件
- [gap15m](../tests/stage015/probe/gap15m.c): cc15m で入れた 33 件が通る
