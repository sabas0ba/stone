# Stage 17 第 1 部: コンパイラをコマンドとして持つ

## 1. なぜここから始めるか

Stage 16 で「実物の `configure` が通る Unix」に到達した
([stage016-os.md](stage016-os.md) 12 章)。そのとき測って判ったことが
1 つある。

**`configure` が外部のプログラムを起動した 6 回は，すべてコンパイラ
関係だった。**

```
gcc -o ./conftest <src>/conftest.c
./conftest              (素 / compiler / version / minor の 4 回)
gcc -Wdeclaration-after-statement -Wunused-result -o a.out -c -xc -
```

道具 (`cat` / `grep` / `uname` …) の側はもう外を必要としていない。
残っているのは **`cc` と書けば動くもの**だけである。

これは [roadmap.md](roadmap.md) 2.1 の 3 案 (A: C++ 処理系 / B: x86-64
backend / C: GCC 4.7 に RV backend) の**どれを採っても等しく要る**。
Stage 16 を挟んだときと同じ理屈で，分岐の判断より先に済ませられる。
**案の決定はまだ行わない。**

## 2. 何が足りなかったか

鎖には `pp16` / `cc15p` / `ld16` があり，どれも完成している。
足りないのは 2 つだけだった。

### 2.1 平らな像しか無かった

`tmp/build/pp16.bin` などは**平らな像**である。QEMU に `-bios` で
0x8000_0000 へ置いて走らせる形で，我々の OS の上の実行形式ではない。

直し方は既にある。**リンカに `'E'` を前置して .o を組み直すだけ**で
ELF になる (Stage 13 の `pp13cmd` / `cc13cmd` / `ld13cmd` と同じ手。
[stage013-tools.md](stage013-tools.md) 7 章)。ソースは要らない。

| | 平らな像 (`-bios`) | OS のコマンド (ELF) |
|---|---|---|
| `pp16` | 53,432 | 53,892 |
| `cc15p` | 248,224 | 248,684 |
| `ld16` | 35,140 | 35,600 |

差はどれも 460 バイトちょうどだが，**中身が同じで包みだけが違う
わけではない**。実際に比べると本体も食い違う。`ld16` は前置部
(`'E'` あり / なし) で入口の作りを変えるからである ——

- 前置部なし: 0x8000_0000 に置かれ M モードで走る。`getc` / `putc` は
  UART を直に叩く
- `'E'` あり: OS の上の U モードで走る。`getc` / `putc` は
  `read(0)` / `write(1)` の 1 バイト版になる ([stage013-tools.md](stage013-tools.md) 7 章)

**同じ `.o` から出るが，同じバイト列にはならない。** 460 という差が
揃っているのは，どちらの前置部も大きさが同じで，ELF の頭のぶんだけ
増えているためである。

### 2.2 3 段を順に呼ぶものが無かった

ホストでは `tools/build.sh` が

```
bundle.sh ヘッダ... src | pp16 > x.i
cc15p < x.i > x.o
{ printf 'E'; cat x.o libc の .o...; printf '\0'; } | ld16 > out
```

と書いている。これを OS の側でやるものが要る。それが `cc17` である。

## 3. 駆動役の作り (`stage017/cc17.c`)

```
cc [-c] [-o OUT] [-W...] [-x c] [FILE | -]
```

段取りは 4 つで，`spawn` するのは 3 回だけである。

1. `/include` の下を読んで束ねを作る (最後に翻訳単位を置く)
2. `pp16` で前処理
3. `cc15p` で翻訳 (`-c` ならここまで)
4. `'E'` + `.o` の連結 + `'\0'` を組んで `ld16` へ流す

### 3.1 束ねは駆動役が自分で書く

ホストの `tools/bundle.sh` もゲストの `stage013/bundle.c` も
**「引数の綴りをそのまま名前にする」**。

```
bundle /include/stdio.h main.c
  -> @/include/stdio.h 1234
```

`#include <stdio.h>` はこれを引けない。sfs が平らだった頃は
「置いてある場所 = 名乗る名前」でよかったが，**sfs2 に階層が
できた今，束ねる側は 2 つを別に持つ必要がある**。

駆動役は「`/include` に置いてあって `stdio.h` と名乗る」ことを
知っているので，ここで組むのが素直である。`bundle` の世代を
足すより呼び出しが 1 つ減る。

### 3.2 `/lib` には 1 揃いだけ置く

駆動役は `/lib` の `.o` を**全部**並べて `ld16` へ渡す。したがって
`/lib` は「1 つのプログラムに対して過不足のない 1 揃い」でなければ
ならない。

最初これで落とした。`src/morecore.o` と `posix/morecore.o` の
両方を置いたところ，`ld` が多重定義で落ちた (終了コード 3)。
2 つは同じものの別実装 (フリースタンディング版と POSIX 版) である。

正しい 1 揃いは `tools/build.sh` が `sh2` を組むときに並べている
10 個である。`tools/build/stage017.sh` の `cc17_run` と
`tests/stage017/test.sh` の `mkroot` は**同じ並びを書いている**ので，
片方を触ったら両方を直す。

### 3.3 警告の選択肢は黙って受ける

`configure` は

```
$cc $OPT1 $OPT2 -o a.out -c -xc - < /dev/null > cc_msg.txt 2>&1
for o in $OPT1; do
  if ! grep -q -- $o cc_msg.txt; then CFLAGS="$CFLAGS $o"; fi
done
```

と書く。**「警告文に選択肢の名前が出てこなければ，その選択肢は
使える」**という判定である。したがって駆動役は `-W...` を受けても
**何も言ってはいけない**。「知らない選択肢だ」と文句を言うと，
`configure` はそれを「使えない」と読む。

`< /dev/null` から読むことにも注意がいる。ここで空装置が要る
([stage016-os.md](stage016-os.md) 11.5)。

## 4. 到達点

`tests/stage017/test.sh` が 5 件を見る。

### 4.1 我々の OS が自分だけで C を翻訳して走らせる

```
hello-build 0
hello from cc on stone
hello-run 0
selfhost-build 0
struct 25
fib 144
sort 1 2 3 4 5
sprintf fmt-42-ff
malloc ok
strlen 10
selfhost-run 0
```

`selfhost.c` は構造体・再帰・`qsort`・`sprintf`・`malloc`・`strlen` を
通す。**`/lib` の 1 揃いが正しく引けているか**が主な狙いで，1 つでも
欠ければリンクで落ちる。期待値はホストの `cc` の出力とも突き合わせる ——
我々の処理系とホストで答えが違うなら，どちらかが間違っている。

### 4.2 `configure` が我々のコンパイラを見つける

```
conftest-build 0
unknown / 0 / 0 / no
C compiler          cc (0.0)
configure-rc 0
CC=cc
CC_NAME=unknown
```

**`CC_NAME=unknown` が要点である。** `configure` はコンパイラを
見つけられなかったとき `cc_name` を空のままにし，`config.mak` には
`gcc` と書く。Stage 16 で `--cc=false` を渡したときはそうなっていた。
`unknown` と書かれているということは，**`conftest.c` を実際に組んで
走らせ，その答えを読んだ**ということである。

`unknown` なのは我々の `cc` が `__GNUC__` も `__TINYC__` も
定義しないからで，故障ではない。`conftest.c` は前処理の定義済み
マクロで相手を見分ける。

所要は 6 秒である (kernel22 の上で `hello` / `selfhost` / `conftest` を
組み，`configure` を 1 回通して)。

### 4.3 CI では走らない部分がある

4.2 は `docs/external/tcc` を要る。外部ソースは repo に取り込まない
決まりなので (`tools/fetch.sh` の頭)，**CI では飛ばす**。
Stage 16 の `configure` の節と同じ扱いである。飛ばしたことは
はっきり出す。

## 5. まだ無いもの

| | 状態 |
|---|---|
| `-I` / `-D` を `pp16` へ渡す | 無い。束ねに何を入れるかは `/include` 決め打ち |
| 複数の翻訳単位 (`cc a.o b.o -o x`) | 無い。1 本だけ |
| `-l` / ライブラリの選択 | 無い。`/lib` を丸ごと並べる |
| `ar` | 無い。`configure` の `AR=ar` はまだ嘘である |
| `__GNUC__` などの定義済みマクロ | 無い。`conftest` は `unknown` と答える |

このうち **`ar` と複数翻訳単位は tcc の `Makefile` を回すのに要る**
ので，次に来る。`-I` / `-D` も同じ理由で要る。

## 6. 次

`configure` が通り，コンパイラが見つかるところまで来た。次は
`make` である。tcc の `Makefile` を我々の OS の上で回せれば，
**外部の実物を我々の OS だけでビルドする**ところまで届く。

[roadmap.md](roadmap.md) 2.1 の 3 案の判断は，そこまで見てから行う。
