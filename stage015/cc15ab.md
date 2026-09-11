# cc15ab --- C コンパイラ 第 15 世代 その 28

`cc15aa` との差は `typedef1()` の 1 か所 1 行 (註を除く)。ソースは
`stage015/cc15ab.sc`。経緯は
[docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.3 の 10。

## 直したもの: 配列型の `typedef`

C89 3.5.6 ——

> `typedef` という記憶域クラス指定子を伴う宣言では、各宣言子は
> 型名を定義する。

**宣言子の形は何でもよい。** したがって `typedef char t[4];` は妥当で、
`t` は `char[4]` の別名になる。

`cc15aa` までは `typedef1()` が**宣言子の後置 `[n]` を読んでいなかった**
ので、名前の次が `[` だと `;` の検査に落ちて 1 (構文誤り) になった。

| 書き方 | `cc15aa` | `cc15ab` |
|---|---|---|
| `char a[4];` (ふつうの宣言) | 通る | 通る |
| `extern char a[sizeof(int)];` | 通る | 通る |
| `typedef int t;` | 通る | 通る |
| `typedef void *F(void *, int);` (関数型) | 通る | 通る |
| `typedef char t[4];` | **1 で拒む** | 通る |
| `typedef int m[2][3];` | **1 で拒む** | 通る |

## 直し方

`pdims()` を呼ぶ 1 行。**ふつうの宣言と同じものを使う。**

```c
  if (tok == t_id) next();
  b = pdims(b);
```

関数型の `typedef` は名前の次が `(` なので `pdims` は素通りする。
置く場所は名前の直後、関数型の括弧を読む前でよい。

## 表に出た形

GCC 4.7.4 の `libcpp/lex.c` 133 行 ——

```c
typedef unsigned long word_type;
typedef char check_word_type_size
  [(sizeof(word_type) == 8 || sizeof(word_type) == 4) * 2 - 1];
```

大きさが 0 以下なら翻訳が落ちる、という翻訳時の表明である。
`libcpp/lex` 1 単位が止まっていた。

## 最初は「定数式の `sizeof`」だと書いた。誤りである

`gcc17.sh where` が指した塊にこの `typedef` があり、目についたのが
`sizeof` だったので、**8.3 の 10 を「配列の大きさの定数式に `sizeof`」と
書いた**。最小の形に落として初めて、落ちているのは `typedef` の宣言子で
あって `sizeof` ではないと判った。

```
extern char a[sizeof(int)];          cc15aa  通る
typedef char t[4];                   cc15aa  1 で拒む
```

定数式の `sizeof` は `cuna()` が `k_sizeof` を受けており、前から通って
いた。

**同じ取り違えを 2 度している** —— `libcpp/identifiers` の `offsetof`
でも、`where` が指した塊の中から目についたものを原因と書いた
([docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.6)。
**`where` は塊までしか絞らない。** 塊の中のどれが原因かは、最小の形に
落として 1 つずつ確かめるまで判らない。

## 鎖は変わらない

既存のソースにこの形は無い。`sh` / `ed` / `mk` を訳した `.o` は
`cc10l` のものと 1 バイトも変わらず、`cc15ab0` と `cc15ab` もバイト一致
する (`tests/stage015/test.sh` が両方を見る)。

## 測り方

- 台帳 `tests/stage015/ledger.txt` に `tdarr` を `ok y\n` で足した。
  **型が作れただけでは足りない** —— `t4 a;` を実際に使い、要素の読み書き・
  `sizeof(t4)`・仮引数に書いたときの先頭要素への decay を見る。多次元
  (`typedef int m[2][3];`) では畳んだ配置がふつうの宣言と同じであること
  (`sizeof` が 24、`g[1][2]` が読めること) を見る。`lex.c` 133 行と同じ
  表明もそのまま置き、関数型の `typedef` を壊していないことも見る
- `tools/diff17.sh` がホストの `gcc` と突き合わせる。ホストも
  `-std=c89 -pedantic-errors` で通し、同じ `y` を出す。**語長に依存しない
  形にしてある** —— `(sizeof(w) == 8 || sizeof(w) == 4)` はホスト (8) でも
  RV32 (4) でも 1 になる

## ビルド

```
sh tools/build.sh stage015
# cc15aa(cc15ab.sc) -> cc15ab0    (1 段目)
# cc15ab0(cc15ab.sc) -> cc15ab    (正本。以降は固定点)
```

SHA-256: ea271c2f0185721210558f8cf97d6b351bfbe7bf05f5290d1217518f9ecb94d7

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
