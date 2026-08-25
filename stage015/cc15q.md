# cc15q 説明文書

cc15q は C コンパイラの Stage 15 第 6 部の世代である。
[cc15p.sc](cc15p.sc) を出発点に，**関数内 `static` の初期化子が文字列
リテラルへのポインタのときに値が壊れる誤り**を直した。差は 1 か所 (6 行)
だけである。

ソース [cc15q.sc](cc15q.sc) が正本である。

## 何が壊れていたか

```c
static void f(void) { static char *p = "hello"; printf("[%s]", p); }
```

`[hello]` ではなく `[L]` が出た。**翻訳は成功し，実行もでき，値だけが
違う** —— 台帳で `bad` と呼ぶ状態である。

文字列プールの flush は 2 系統ある。関数本体側は `spfn` の分だけ，
`gstrflush()` は `gspn` の分だけ `lsoff` を埋める。ところが
`gstrflush()` は呼び出し 3 か所すべてが `if (!ginfn)` で守られているので，
関数内 `static` のときは一度も呼ばれない。**任せた先が受け取っていな
かった。**

直しは本体側の flush の後ろで `gspn` の分も埋めるだけである。静的の
文字列は `plocal()` で先に積まれるので同じプールの中にあり，`gspofs` は
そのまま使える。

詳細は [../docs/stage017-cc.md](../docs/stage017-cc.md) 24〜25 章。

## なぜ気づかなかったか

**我々自身がこの形を使っていない。** 鎖のソース全体を探して 0 件だった。
tcc の `tccgen.c` の `parse_atomic()` が

```c
static const char *const templates[] = { "alm.?", "Asm.v", ... };
```

という形で使っており，そこで初めて表に出た。適合台帳の `staticinit` は
整数の初期化子しか見ていなかったので漏れた。`staticstr` を足してある。

## ビルド

```
sh tools/build.sh stage015
# cc15p(cc15q.sc) -> cc15q0     (1 段目)
# cc15q0(cc15q.sc) -> cc15q     (正本。以降は固定点)
```

SHA-256: 9a809bfd16a272985c30d7642f767849222862bea72818de156fc10aabdbacee

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 鎖の成果物は変わらない

既存のソースはこの形を使っていないので，出るコードは変わらない。
`stage013/sh.c` `ed.c` `mk.c` の `.o` が cc15p のものと**バイト一致**
することを確かめた。固定点 (B2 == B3) も成立する。
