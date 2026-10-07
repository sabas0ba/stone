# cc15aq --- C コンパイラ 第 15 世代 その 43

`cc15ap` との差は，関数へのポインタの型が仮引数の情報を持つことと，64 bit の `++` / `--` である (註を除く)。ソースは `stage015/cc15aq.sc`。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.15。

GCC の cc1 を stone の OS の上で走らせて見つけた。`unsigned long long / 7` を -O0 で訳すと cc1 が NULL を引いて落ちた。

## 直したもの: 関数へのポインタを通した呼出しの実引数の変換

実引数は仮引数の型へ変換して渡す (C89 6.3.2.2)。名前つきの呼出しは，宣言された仮引数の語の並びと，64 bit 整数・double・float の位置を見て変換してから積む。`cc15ap` までは関数へのポインタの型が返却型しか持たなかったので，ポインタを通した呼出しは実引数を**そのまま**積んでいた。

我々の呼出し規約では呼ばれた側が語を数えて取り出す。幅が違うと，以降の実引数がすべてずれる。

GCC の cc1 は target hook の表 (`targetm`) を関数へのポインタで呼ぶ。`emit_call_1` は

<!--
SPDX-SnippetBegin
SPDX-SnippetCopyrightText: Free Software Foundation, Inc.
SPDX-License-Identifier: GPL-3.0-or-later
-->
```c
targetm.calls.return_pops_args (fndecl, funtype, stack_size)
```

<!-- SPDX-SnippetEnd -->

と呼び，`stack_size` は `HOST_WIDE_INT` (64 bit)，仮引数は `int` である。2 語を積んだので，呼ばれた側 (`ix86_return_pops_args`) の `funtype` には別の語が入り，`ix86_get_callcvt` が NULL の型を引いて落ちた。libcall (`__udivdi3`) を作る経路で初めて表に出た。

| 変換 | `cc15ap` | `cc15aq` |
|---|---|---|
| 64 bit -> int の仮引数 | 2 語を積む (以降がずれる) | 下位語を積む |
| int -> 64 bit の仮引数 | 1 語を積む (以降がずれる) | 符号に応じて広げる |
| int -> double / double -> float / double -> int | 変換しない | 変換する |
| 可変長の型のポインタで呼ぶ | 実引数を順に積む | 名前つきの部分を変換し，実引数は順に積む (下の「可変長の型のポインタ」) |
| 個数 | 検査しない | 仮引数の情報があれば検査する (5) |

## 型の持ち方

関数型の表 (基底 `t_fn` 以降) に，返却型と並べて仮引数の語数・3 つの位置・可変長かを持たせる。組が同じなら同じ型番号である。表は 1024 から 8192 に広げた。

- 空の括弧 `()` と K&R 形式の名前の並びは仮引数について何も言わない (C89 6.5.4.3)。語数を -1 にし，従来どおり変換しない
- 関数へのポインタの宣言子・関数型の typedef・関数型の仮引数は，仮引数並びを読み飛ばさずに読む (`fnproto`)。局所記号は作らない。仮引数の中の関数ポインタを読むと宣言子の名前 (`fpnam`) が書き換わるので退避して戻す。戻さないと `typedef tree (*walk_tree_lh) (tree *, int *, tree (*) (tree *, int *, void *), ...)` (GCC の tree.h) の名前が消え，gcc/ の 307 単位が 1 で止まった
- 関数名を値として使うと，その関数の宣言の情報を持つ型になる。`(*f)(x)` も変換して積む
- 関数型の typedef で宣言した関数 (`static refmarker_fn f;`) は，型の情報を宣言として持つ
- 名前つきの呼出しとポインタを通した呼出しは同じ変換 (`argconv`) と積み方 (`argpush`) を使う

## 可変長の型のポインタ —— GCC の GEN_FCN に合わせる

GCC は命令の生成関数を `GEN_FCN (icode) (op0, op1, op2)` で呼ぶ。表の型は `typedef rtx (*insn_gen_fn) (rtx, ...);` (可変長) で，指す先の `gen_addsi3` などは固定個の仮引数を持つ。C としては未定義の動作だが，広く使われる呼出し規約では可変長と固定個の積み方が同じなので成り立つ。

我々の可変長の積み方 (可変部を逆順に先に積む。呼ばれた側は可変部の個数を知らないので，名前つきの位置を固定するためにこうしている) は固定個と違う。最初の版は可変長の型のポインタで可変部を逆順に積んだので，`gen_*` の実引数が逆になり，cc1 が `t.c` の 1 行の関数でも内部の検査 (`gcc_assert`。dwarf2cfi.c / i386.c) で止まった。

両方を満たす積み方は無いので，GCC の使い方に合わせる。可変長の型のポインタでは名前つきの部分だけ仮引数の型へ変換し，実引数は固定個と同じく順に積む。**本物の可変長の関数をポインタで呼ぶ形は受けない** (`cc15ap` までと同じ。値が誤る)。

変換が効く箇所は，間接呼出しの変換だけを外した版と `.o` を比べて確かめた。全 1282 単位のうち 17 単位で，GEN_FCN・target hook (64 bit -> int など)・libiberty の simple-object・GMP の doscan などの呼出しだった。

## 直したもの 2: 関数型の typedef で書いた仮引数

`static int apply(binop_t f, int a, int b)` (`binop_t` は関数型の typedef) の `f` は関数へのポインタになる (C89 6.7.1)。`cc15ap` までは関数型のまま登録したので，`f(a, b)` を 5 で拒んでいた。

## 直したもの 3: 64 bit の ++ / --

`incdec` は 64 bit の型でも下位語だけを読み書きしていた。下位語が 0xffffffff と 0 の間を回るときに上位語へ桁上がり (桁借り) せず，式の値の上位語 (`ehi`) は前の式の残りだった。

cc1 は -O2 で記憶域を使い果たした (`out of memory allocating 33558527 bytes after a total of 89875152 bytes`)。gdb で `xmalloc_failed` に止めて枠を辿ると，ivopts の `multiplier_allowed_in_address_p` の

<!--
SPDX-SnippetBegin
SPDX-SnippetCopyrightText: Free Software Foundation, Inc.
SPDX-License-Identifier: GPL-3.0-or-later
-->
```c
for (i = -MAX_RATIO; i <= MAX_RATIO; i++)      /* i は HOST_WIDE_INT */
  XEXP (addr, 1) = gen_int_mode (i, address_mode);
```

<!-- SPDX-SnippetEnd -->

が -1 から 0 へ進めず，CONST_INT の表を広げ続けていた。2 語で読み，加減算の 64 bit の経路 (`ll_addsub`) で計算して 2 語書く。後置は前の 2 語を，前置は後の 2 語を式の値にする。

## ビルドチェーンは変わらない

`sh` / `ed` / `mk` を訳した `.o` は `cc10l` のものと 1 バイトも変わらない。これらのソースは関数へのポインタを通して幅の違う実引数を渡さない。

## 測り方

`tests/stage015/probe/fpll.c` は構造体のメンバ・局所・配列の関数へのポインタ，関数型の typedef，関数型の仮引数，`(*f)(x)`，仮引数に関数ポインタを持つ関数ポインタの typedef，可変長の型のポインタで固定個の関数を呼ぶ形 (GEN_FCN) を通して，上の表の変換を値で見る。

| | `cc15ap` | `cc15aq` |
|---|---|---|
| `tools/diff17.sh fpll` | **我々だけが拒む** (5。関数型の typedef の仮引数) | 値が一致 |
| `tools/diff17.sh llinc` | **値が違う** (ループが止まらない) | 値が一致 |
| cc1 で `unsigned long long / 7` を -O0 で訳す | `ix86_get_callcvt` で NULL を引く | (8.15 の測定) |

## ビルド

```
sh tools/build.sh stage015
# cc15ap(cc15aq.sc) -> cc15aq0    (1 段目)
# cc15aq0(cc15aq.sc) -> cc15aq    (2 段目。以降は固定点)
```

SHA-256: 759b2d353402090655fc44306eacfcc30ad0223d28c59fc0e1209e95c7720c37

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
