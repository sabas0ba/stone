# Stage 17 第 5 部: GCC への道を決める (roadmap 2.1)

## 0. 一度書き直している

**本書の初版は roadmap を読み違えていた。** 3 案をすべて「tcc で GCC を
訳す」と書いていたが，roadmap の 3 案は**我々の器が何を獲得するか**を
書いたものである。持ち主の指摘で書き直した (経緯は
[stage017-cc.md](stage017-cc.md) 34 章)。

**L1 の主語は我々の器である。**「GCC 自体をコンパイルできること」の
「できる」のは我々であって，tcc ではない。tcc・GCC・Linux は
**我々の器の成熟度を測る的**であり，道具ではない
([roadmap.md](roadmap.md) 71 行 / [artifacts.md](artifacts.md) 3 章)。

## 1. なぜ今か

roadmap 2.1 は 3 案の判断を「tcc の `Makefile` を我々の OS の上で
回せるようになるまで」保留すると書いていた。その条件は満たされた
([stage017-cc.md](stage017-cc.md) 21 章・29 章)。

2026-08-28に案Aを選択した。本書は、選択前の比較材料と、案Aを進めるための測定結果を記録する。

## 2. 測れていること —— **すべて我々の器の話である**

| | 中身 | 出典 |
|---|---|---|
| 我々の器が tcc を訳せる | 我々の cc が tcc を訳し，出た tcc が自分自身を訳し，答が参照実装とバイト一致する (T2 == T3 == tccH) | roadmap 3 (Stage 15) |
| 我々の OS の上で訳せる | 上流の `Makefile` を我々の `mk20` が回し，我々の `cc19` が全単位を訳す。出た `tcc` は手で命令を並べたものとバイト一致 | 21 章 |
| 書庫まで作れる | `lib/Makefile` が `lib 0` で終わり，員 10 本の `libtcc1.a` が出る | 28〜29 章 |
| 測ると我々の穴が出る | tcc のソースを読ませたことで C 適合の誤りが 4 つ出た (`cc15q` / `cc15r` / `cc15s`)。さらに zlib / bzip2 で 2 つ (`cc15t` / `cc15u`)。Stage 15 の台帳から「通るが誤り」が 0 になった | 25 / 28 / 31 / 33 章 / 5.1 |
| 実物が組めて走る | zlib 1.3.1 と bzip2 1.0.8 を我々の OS の上で我々の器が訳し，我々の ar が書庫にし，我々の ld が繋ぎ，走らせて往復した。値はホストの gcc と一致 | 5.1 |
| libc の穴も出る | `sys/types.h` / `signal.h` / `memchr` が無く，`strerror` と `lseek` は実体があるのに宣言が無かった。`fopen(path,"a")` は先頭を潰していた | 32.3 (`libc21`) |
| OS の側 | 木を持つファイル系 (sfs3。時刻つき)・255 MiB の記憶域・POSIX 部分集合のシェル (`sh2`)・組込みの道具 10 個・差分ビルドする `mk20` | stage016-os.md / 11 章 |

**要するに，我々の器は「tcc の大きさの実物を，上流の `Makefile` の
ままビルドできる」ところまで来ている。** そして**測るたびに我々の穴が
出る**というのが，的の本来の働きである。

## 3. 3 案を，いま判っていることに当てる

### 3.1 変わらない事実 (roadmap 2.1 の前提)

- GCC の RISC-V backend が入ったのは **GCC 7**
- C だけでビルドできる最後の GCC は **4.7.4** (4.8 以降は C++)

この 2 つが交わらないことが，そもそもの障害である。

### 3.2 3 案は「我々が何を獲得するか」である

| 案 | **我々が獲得するもの** | 得失 |
|---|---|---|
| A | **C++ の部分集合を訳せる器** | RISC-V を維持できる。C++ は C より桁違いに大きい |
| B | **我々の器の x86-64 backend** | 実績のある経路。ただし RV32 が主対象でなくなる |
| C | **GCC 4.7 に載せる RISC-V backend** (我々が書く) | GCC 本体の fork を保守することになる |

**どの案でも，GCC を訳すのは我々の器である。** 案 C で「GCC 4.7 は C
だから訳しやすい」と言えるのは，**我々の器が C を訳せるから**であって，
他所に C 処理系があるからではない。

**持ち主の判断は案 A である** (6 章)。以下 3.3〜3.4 は判断の材料として
残す。

### 3.3 したがって，いちばん先に測るべきことが変わる

初版は「GCC 4.7 が tcc で訳せるか」を 4.1 に置いていた。**それは我々の
指標ではない。** 置くべきはこれである。

> **我々の器 (`cc19` + `cc15u`) が GCC 4.7 の C を訳せるか。**

tcc を訳せたことは足場になる。tcc は C99 を相当に使う実物で，それを
訳せたのだから，GCC 4.7 の C も「まったく手が届かない」わけではない。
**しかし tcc と GCC 4.7 は桁が違う** —— そこを数えるのが 4 章である。

### 3.4 案 C の副産物についての訂正

初版は「案 C を採ると副産物として RV32 を吐く `g++` が手に入り，それで
GCC 7 を訳せば案 A の目的に C++ 処理系を自作せずに届く」と書いた。

**筋としては成り立つ** —— GCC 4.7 の木には `cc1plus` が入っており，
それも C で書かれているので，**我々の器が訳せれば**我々の鎖の成果物に
なる。tcc を経由する必要はない。

ただし前提が 2 つある。

1. **我々の器が GCC 4.7 の C を訳せること** (3.3)
2. **C++ の実行時** (`libstdc++` / `libsupc++`) をどうするか (4.4)

2 は案 A を採っても消えない，3 案に共通の未測項である。

## 4. 案Aを進めるために測ること

案Aの選択後も、GCC規模のsource tree、build system、C++ runtimeに対する未測定項目が残っている。4.5の取得と容量測定は完了した。

### 4.1 我々の器が GCC 4.7 の C を訳せるか

**これが最初の 1 つである。** tcc のときと同じ測り方ができる ——
ソースを読ませ，**通らなかった単位とその理由を数える**。tcc では
それが「C 適合の誤り 4 つ + libc の穴 6 つ」という表になった
(31〜33 章)。GCC 4.7 でも同じ形の表が出るはずで，**その表の長さが
案 C の見積りそのもの**になる。

**測った (8 章)。** libiberty と libcpp の 70 単位のうち **48 単位**が
`.o` まで通り，表は「C 適合の穴 11 つ + libc の穴 4 つ + 器の上限 2 つ」に
なった。予想どおり同じ形の表が出ている。**表は測るたびに動く** ——
前処理器の未対応 1 つ (`defined`) は `pp18` で，C 適合の穴の 1 つ
(仮引数並びの抽象宣言子) は `cc15w` で埋め，そのたびに次の壁が出た。

### 4.2 GCC 4.7 の `configure` と `Makefile` が我々の OS で回るか

Stage 16 の教訓は「**律速はコンパイラではなく OS だった**」である。
tcc の `configure` は通ったが，GCC の `configure` は桁が違う。我々の
`sh2` は POSIX の部分集合で，道具は 10 個，`awk` は無い。

上流の `configure` と `Makefile` が何を要求するかを静的に数え，
足りないものを表にする (stage016-os.md 11 章・21 章と同じ手順)。

### 4.3 backend を 1 つ足す仕事の大きさ

`.md` の行数・対応が要る hook の数を数えれば見積もれる。案 B と案 C の
どちらでもこの数が効く。

### 4.4 C++ の実行時をどうするか

案 A でも案 C でも，C++ を訳せる器が出来ただけでは `g++` は使えない。
対象の側の `libstdc++` / `libsupc++` とそのヘッダが要る。我々の OS に
あるのは C の libc だけである。

道は 2 つあり，**どちらも測っていない**。

| | 中身 | 大きさの見当 |
|---|---|---|
| 実行時を移す | `libstdc++-v3` を我々の OS 向けに組む | `configure` が大きく，libc に要求するものも多い |
| 要らないことを示す | GCC 7 の bootstrap が実行時のどこまでを使うかを数える | 静的に数えられる。4.2 と同じ手順 |

後者から測るのが順である。GCC 自身は例外も RTTI も使わない方針で
書かれているので `libsupc++` の一部で足りる見込みはあるが，
**見込みは測りではない**。

### 4.5 GCC 4.7.4の取得と容量測定 — 完了

GNU公式配布元の`https://ftp.gnu.org/gnu/gcc/gcc-4.7.4/gcc-4.7.4.tar.bz2`を取得し、SHA-256 `92e61c6dc3a0a449e62d72a38185fda550168a86702dea07125ebd3ec3996282`で固定した。取得と照合は`sh tools/fetch.sh gcc47`、容量測定は`sh tools/gcc17.sh measure`で再現できる。

| 測定項目 | 結果 |
|---|---:|
| file | 76,693 |
| directory | 4,663 (rootを含む) |
| symbolic link | 0 |
| その他のsfs3未対応entry | 0 |
| hard linkを持つfile path | 0 |
| file内容の合計 | 467,486,875 bytes |
| sfs3のtableと4-byte alignmentを含む最小image | 473,459,336 bytes |
| 最長のcomponent | 92 bytes |
| sfs3の47-byte上限を超えるcomponent | 248 |
| 最深path | 12 components |
| kernel24のsfs領域 (`SFSA`から`UBASE`) | 33,554,432 bytes |

`sfs3.sh pack`が表現できないsymbolic linkその他のentryは、公式archiveと展開後のtreeのどちらにも無かった。hard linkも無い。`gcc17.sh measure`はこれらを明示的に数え、未対応entryが1つでもあれば最小image容量とkernel24への収容可否を`unknown`とする。したがって、黙って欠落させたtreeの容量を全treeの容量として扱うことはない。

全配布木は現在のsfs3/kernel24に収まらない。さらに`libstdc++-v3/include`だけを見ても50-byteのcomponentがあり、3 headerが現在の47-byte上限を超える。documentation、testsuite、Java frontendを除外するだけでは名前長の問題は解消しない。

**この測定に対する答が7章のsfs4/kernel25である。** 同じ`gcc17.sh measure`を新世代に当てた値は[tests/stage017/expected/gcc47-tree.txt](../tests/stage017/expected/gcc47-tree.txt)にある。名前は103 bytesまで、窓は536,870,912 bytesになり、最小imageは478,015,272 bytesで収まる。

## 5. どの案でも等しく要るもの

判断を待つ間に進められるもの。**どれも主語は我々の器である。**

### 5.1 実物を，我々の器で，我々の OS の上で組む —— **済**

`zlib` 1.3.1 (15 単位) と `bzip2` 1.0.8 (7 単位) を，**我々の OS の上で
我々の器が**訳し，**我々の `ar` が書庫にまとめ**，**我々の `ld` が繋ぎ**，
**走らせて往復させた**。手順は `tools/ext17.sh run`。

**初版はここを「tcc で組む」と書いていた。撤回した** —— それでは tcc の
成熟度を測ることになり，我々の値が出ない (34 章)。外部のソースは素材で
あって，道具ではない。

```
cc19 -c z/deflate.c -o z/deflate.o -I z    ... 22 単位
ar rcs z/libz.a z/*.o                       我々の ar17
cc19 -o zt z/zt.c z/libz.a -I z             我々の cc19 + ld
zt                                          我々の kernel24 の上で走る
```

駆動 (`tests/stage017/ext/zt.c` / `bzt.c`) は**我々が書いたもの**である。
`compress2` / `uncompress` / `crc32` / `adler32` と
`BZ2_bzBuffToBuffCompress` / `Decompress` を呼び，元に戻ることを見る。

#### zlib 自身の検査も通した

**我々が書いた駆動は，我々が思いついた道しか通らない。** `zlib` は
自分の検査 (`test/example.c`) を持っているので，それも**入力として
読ませ**，我々の器で組んで走らせた。

`gzopen` / `gzputs` / `gzprintf` / `gzread` / `gzseek` / `gztell` /
`gzgetc` / `gzungetc` / `gzgets` / `deflateSetDictionary` /
`inflateSync` / 辞書つき伸長まで通る。**`gz*` の系統はファイルを開いて
読み書きするので，`libc` のファイル層まで一緒に測れる。**

```
zlib version 1.3.1 = 0x1310, compile flags = 0x55   ← 我々 (RV32)
uncompress(): hello, hello!
gzread(): hello, hello!
gzgets() after gzseek:  hello!
inflate(): hello, hello!
large_inflate(): OK
after inflateSync(): hello, hello!
inflate with dictionary: hello, hello!
```

**ホストで同じソースを組んだものと，1 行ずつ一致した。** 唯一違うのは
`compile flags` で，これは `uInt` / `uLong` / `voidpf` / `z_off_t` の
大きさを畳んだ値なので RV32 (`0x55`) と x86-64 (`0xa9`) で必ず違う ——
**違ってよい理由が言える 1 行だけが違う**。

#### 出た表

| 出たもの | 中身 | 直し |
|---|---|---|
| C 適合の穴 | 型修飾子が宣言指定子の列の**途中**に来る形 (`unsigned const char`) を拒む。C89 6.5 は順序を縛らない | `cc15t` |
| **C 適合の誤り** | **複合代入 (`a op= b`) が符号を見ていない。** `unsigned` の `%=` が符号つき剰余に，`int` の `>>=` が論理シフトになる | `cc15u` |
| libc の穴 | `libc21` を **`.o` にしていなかった** —— ヘッダだけ足して満足していたので，新しく実装したもの (`signal` / `memchr` / `strerror`，`fopen(path,"a")` の直し) はどこにも無かった | `l21_*` を組む |
| 道具の穴 | **`ld` が落ちた理由を言わない。** 未定義シンボルを `exit(2)` で拒むだけで，`cc19` に出るのは "cc: link failed" の 1 行 | `ld17` |
| /lib の組合せ | `ctype` を置いていなかった (tcc の作業場では要らなかった)。`bzlib.c` が `isdigit` を呼ぶ | `ext17.sh` |

**`ld17` は足した直後に効いた。** `bzip2` の結合が落ちたとき，`ld16` は
黙って `exit(2)` するので判るのは「どれかが足りない」まで。`ld17` は
`ld: undefined symbol: isdigit` と言った。**測る道具に目が無いと，
測定そのものが止まる。**

#### 往復するだけでは足りなかった

いちばん大きい収穫は `cc15u` で，**それは往復の検査では捕まらなかった**。
往路と復路で同じ誤りが起きれば，中身が違っても元に戻る。

捕まえたのは**外の物差しとの突き合わせ**である。ホストの gcc に
**同じソースと同じ駆動**を組ませ，値を並べた。

```
        crc32     adler32     level 1/5/9 の長さ
我々    282d245a  43dc7153    3042 / 1537 / 1537
ホスト  282d245a  459e7153    3042 / 1537 / 1537
```

`crc32` も圧縮長も一致するのに `adler32` だけ違い，しかも**下位 16 bit
は合っていて上位だけ**違う。adler32 の下位は最大 142 万，上位は約 39 億
—— **上位だけが 2^31 を超える**。出方がそのまま原因 (符号つき剰余) を
指していた。

**この突き合わせは `tools/ext17.sh run` に入れてある。** 期待値は
ホストの gcc と Python の `zlib` (別実装) で同じになることを確かめた。

#### 4.1 の予行として

出た表は 4.1 と同じ形である。**GCC の素材が無くても予行になる**と
書いたとおりになった —— 22 単位という tcc より一段小さい的でも，
C 適合の誤りが 1 つ・穴が 1 つ・libc と道具の穴が 3 つ出た。

### 5.2 我々の器の C 適合を，測って埋め続ける

tcc を読ませて 4 つ出た (25 / 28 / 31 / 33 章)。`zlib` / `bzip2` を
読ませて 2 つ出た (`cc15t` / `cc15u`)。GCC 4.7 を読ませれば，同じように
出る。**台帳 (`tests/stage015` / `tests/stage014`) に載せて埋める**のが，
どの案でも要る仕事である。

`cc15u` が示したのは，**自分自身を組む限り永久に表に出ない誤りがある**
ということである。我々のソースは `>>=` / `/=` / `%=` を 1 つも使って
いない。固定点も再現性もバイト一致も，この誤りを 1 つも捕まえなかった。
**外を的として読ませる以外に見つける道が無い。** ベンチマークを置く
理由がこれである。

#### 台帳の期待値は我々が書いている

`cc15u` を捕まえたのは「ホストの gcc に同じソースを組ませて値を並べた」
という**1 回限りの手作業**だった。それを仕組みにしたのが
`tools/diff17.sh` である。

```
sh tools/diff17.sh          プローブ全部
sh tools/diff17.sh <名前>   1 つだけ
```

同じソースを我々の鎖とホストの処理系の両方に訳させ，走らせて標準出力を
突き合わせる。**我々が拒む形**については，
`gcc -std=c89 -pedantic-errors` が**エラーにするか**を見る ——
台帳の `gap` が「未対応」ではなく「誤った入力を正しく拒む」だと名乗る
には，**その入力が本当に誤っていることを我々以外が言っている**必要が
あるからである。

**警告の数を数えるのでは弱い。** gcc は正しいソースにも
`-Wmissing-braces` のような書き方の助言を出す。制約違反にしか出ない
判定が要る。

##### 仕掛けた初回に 2 つ出た

| | 中身 | 直し |
|---|---|---|
| **我々の誤り** | スカラの初期化子を波括弧で囲んだうえ中身が 2 つ以上ある形 (`int a[2][2] = {1,{2,3},4}`) を黙って通し，溢れを隣へ入れていた | `cc15v` |
| **検査の誤り** | `probe/qualorder.c` が `const` の付いた対象へ**代入**していた。C89 6.3.16 の制約違反で，我々が修飾子を検査に使わないから通っていただけ | プローブを直した |

1 つ目は，`stage015/cc15s.md` の表に**「正」と書いてあった**ものである。
我々が出した値を我々が正しいと決めていた。ホストは診断を 2 つ出したうえで
別の値 (`1 2 4 0`) にしており，**制約違反に黙って値を作っていたのは我々
だけ**だった。

2 つ目は，**差分試験を入れなければ「同じソース」と言えなかった**ことを
意味する。ホストで訳せないプローブは，そもそも比べる相手になっていない。

##### ホストは万能の物差しではない

飛ばすものは**名前と理由を必ず出す** —— 黙って飛ばすと「全部合った」に
見える。

| 飛ばす | 理由 |
|---|---|
| `strsizeof` / `layout` | 語長・構造体配置が RV32 の規則。ホストは x86-64 |
| `lldiv` | 0 除算を見る。C が定義していない (ホストは SIGFPE) |
| `fpsoft` | 鎖の内部の名前 (`__dadd` …) を直に呼ぶ |
| `hyg16` | 前処理器の検査。訳して走らせるものではない |
| `layout-oracle` | `main` を持たない断片 |
| `strtod` | libc を繋いで OS の上で走らせる形。**次に広げるならここ** |

いまは 22 一致 / 0 食い違い / 7 飛ばし。走行は 90 秒ほどである。

## 6. 決定と次の作業

| | 決定 |
|---|---|
| 軸 | 案A。stone toolchainにC++ subsetを実装する |
| GCC 4.7.4 | GNU公式配布元から取得し、SHA-256で固定済み |
| filesystem | GCCのbuildに着手する前にsfsとkernelの更新が必要 |

### 6.1 作業順序

1. ~~GCC 4.7.4を固定付きで取得する。~~ 完了。
2. ~~配布木をsfs3/kernel24の上限と比較する。~~ 完了。現在の構成には収まらない。
3. ~~sfsの名前長とkernelのmemory mapを更新する。~~ 完了。7章 (sfs4 / kernel25)。
4. ~~GCC 4.7.4のC translation unitをstone toolchainへ入力し、C対応とlibcの不足を記録する。~~ libiberty / libcppは完了 (8章)。70単位のうち42単位が`.o`まで通り、C適合の穴が8つ、libcの穴が3つ、前処理器の未対応が1つ、器の上限が1つ出た。gcc/本体 (362単位) は8.7の1〜2を埋めてから測る。
5. GCC 7のbuildに必要なC++ subsetとruntime symbolを静的に測定し、案Aの実装範囲を決める。

### 6.2 sfsと差分build

`sfs3`はmtimeをnanosecond単位で保持し、`mk20`はその値からdependencyの更新を判定できる。この機能は既存testで確認済みであり、差分buildの判定ロジック自体に変更は不要である。

一方、前版の「項目数とimage sizeは`pack`の引数なので拡張できる」という記述は不正確だった。host側の`pack`は値を受け取れるが、kernel24がsfsに割り当てるaddress spaceは`0x84000000`から`0x86000000`までの32 MiBに限られる。GCC 4.7.4の配布木が必要とする最小imageは473,459,336 bytesであり、`pack`の引数だけでは解決しない。

したがって、差分build機能を作り直す必要はないが、GCCのbuildに進む前にsfs formatとkernel memory mapの世代更新が必要である。少なくともcomponentを50 bytes以上保持し、測定用source treeとbuild outputを同時に置ける容量を定義する。

**この結論に対する実装が7章である。** 差分buildの判定は1行も変えていない。

## 7. sfs4 と kernel25 —— 長い名前と広い窓 (6.1 の 3)

4.5 の測定に対する答である。**変えたのは名前の枠と像の置き場だけ**で、
差分buildの判定も、syscallの並びも、userlandも1行も変えていない。

### 7.1 sfs4

| | sfs3 | sfs4 |
|---|---:|---:|
| 項目の幅 | 72 bytes | 128 bytes |
| 名前の枠 | 48 bytes | 104 bytes |
| 名前の上限 | 47 bytes | 103 bytes |
| 頭の32 bytes | magic / size / tbloff / count / cursor | 同じ |
| 名前より後ろの6語 | parent / dataoff / len / flags / mtlo / mthi | 同じ順 |

magicは`sfs4`。道具は[tools/sfs4.sh](../tools/sfs4.sh)で、`pack` / `unpack` /
`list`の引数はsfs3と同じである。

**なぜ103か。** 項目を128 bytes = 2の冪にすると、索引から位置を出す算術が
乗算ではなく左shiftで済む。128から後ろの6語 (24 bytes) を引くと名前の枠が
104 bytes、終端の分を除いて103 bytesになる。実測の92 bytesに11 bytesの余りが
残る。

**長すぎる名前は黙って切らずに拒む。** 切ると同名衝突が起き、別のfileを
上書きする。104 bytesのcomponentは`name too long`で名指しで落とす。

**深さ10以上でpackが落ちていたのを直した。** `pack`はdirectoryを浅い順に
並べてから作るが、深さの印を0詰めせずに並べると文字列として比べられ、
`"10"`が`"2"`より前に来る。親より先に子が出て`parent not found`で落ちる。
tccの木は深さ6だったので表に出ていなかったが、GCC 4.7.4は深さ12なので
1 fileも載らなかった。`tools/sfs2.sh` / `sfs3.sh`にも同じ誤りがあったので
同時に直した。深さ9までの木では並び順が変わらないので、既存のimageは
1 byteも変わらない。

### 7.2 kernel25

kernel24の写しに`#define`と起動時の検査を入れただけである。

```
                kernel24        kernel25
  SFSA          0x8400_0000  -> 0xa000_0000     sfs image
  窓の上端      0x8600_0000     0xc000_0000     (SFSTOPを新設)
  窓の大きさ    33,554,432   -> 536,870,912 bytes
  RAM           512 MiB      -> 1 GiB
  PATHMAX       63           -> 255 bytes         (statat / spawn)
```

**名前を広げただけでは足りない。** 名前1段が103 bytes持てても、経路を受ける
器が63 bytesなら、92 bytesの名前は**rootに置いてもstatできない**。`open`は
経路をそのまま辿るので`cat`だけでは表に出ず、`stat`で初めて出る。GCC 4.7.4の
木は最長の経路が131 bytes・深さ12なので、余りを足して255にした。`mk20`の差分
buildは`statat`を使うので、ここが塞がっていると測定用treeを置いてもbuildできない。

**なぜ上へ移したか。** `0x8600_0000`より下は1 byteも動かせない。trap frame
(`0x8370_0000`)・kernel stack (`0x8380_0000`)・user像のload先
(`0x8600_0000`) はld16の前置部に機械語として焼き込まれており、動かすと
linkerの世代が要る。kernel19が記憶域を広げたときと同じ制約である。
上へ伸ばすしかなく、`SAVETOP` (`0xa000_0000`) の先が唯一の空きだった。

`0x8400_0000`〜`0x8600_0000`の32 MiBが空いた。`0x8100_0000`〜`0x8370_0000`と
合わせ、当面は使わない。

**kernel24とsfs3はそのまま残す。** `tools/tcc17.sh`と`tools/ext17.sh`
(tcc・zlib・bzip2をbuildする道) は32 MiBで足りており、動かす理由が無い。
凍結した世代は凍結したまま再現できることが、この鎖の前提である
([artifacts.md](artifacts.md))。kernel25へ移すのは、移す必要が測定で出て
からにする。

**窓に入らないimageは載せない。** 頭の大きさの欄を見て、窓を超えるなら`S`を
出して止まる。黙って載せると、はみ出した先は何も無い番地なので、書いた中身が
消える。形の違うimage (sfs3) は`?`で止まる。

### 7.3 測り直した結果

| | 値 |
|---|---:|
| GCC 4.7.4の最小image | 478,015,272 bytes |
| kernel25の窓 | 536,870,912 bytes |
| 余り | 58,855,640 bytes |
| 103 bytesを超えるcomponent | 0 |

**全配布木が窓に収まる。** ただし余りは56 MiBしかなく、build outputを同じ像に
置く余地は小さい。6.1の4に進むときは、必要なtranslation unitだけを取り出した
測定用treeを使う。

### 7.4 表の空き枠 —— 最小imageは「読むだけ」の値である

最小imageは表を実測ちょうど (81,356項目) で切った値である。**この幅で詰めると
guestはfileを1つも作れない。** GCCを組む間に出る`.o`や`.a`は表に項目を要るので、
詰める側は最初から余りを持たせる必要がある。

`tools/gcc17.sh`は作業用の項目数を2^17 = 131,072に置いている。実測の81,356を
超える最小の2の冪で、項目幅が128 bytesなので表が16 MiB丁度になる。

| | 値 |
|---|---:|
| 作業用の表の項目数 | 131,072 |
| 空き項目 | 49,716 |
| 表 + 中身の使用量 | 484,378,920 bytes |
| 窓に残るdata領域 | 52,491,992 bytes |

**表を広げても窓には収まる。** 表が16 MiBに増える分だけdata領域の余りが
58,855,640から52,491,992 bytesへ減るが、全配布木は依然として載る。

`STONE_SFS4_WORKSPACE_ENTRIES`でこの項目数を下げられる。詰める経路そのものを
現実的な時間で検査するためで、測定値を出すときは使わない。

### 7.5 見積りではなく実際に詰めて確かめる

```sh
sh tools/gcc17.sh pack
```

窓と同じ大きさ (512 MiB) のsfs4に全配布木を詰め、magic・使用量・経路を
検算する。**見積りと実物は別である。** `measure`は表の幅と詰めた大きさから
下限を出すだけで、その規模をpackが通せるかは見ていない。深さ12・81,355経路と
いう規模では、並び順や親の索引付けなど計算に出ない所で落ちうる —— 実際に
深さ10以上で落ちる誤りは後から見つかっている (7.1)。

経路は詰めたimageを`list`で読み直し、種別と経路の集合をそのまま突き合わせる。
書く側と読む側の両方を通さないと、親の索引付けの誤りが表に出ない。

**数を数えるだけでは足りない。** 親の索引が1つずれても項目は有効なまま残る
ので、`list`は同じ行数を出す。実際に`d1/b`の親を1つ隣に書き換えると、経路は
`d1/d2/b`に変わるのに行数は変わらない。数が合ったまま、guestから見える木だけが
別物になる。重複した経路や種別の取り違えも同じく数には出ない。

**CIでは回さない。** `tools/sfs4.sh`のpackは項目ごとに`dd`と`od`を呼ぶPOSIX
shellであり、81,356項目では1時間半かかる (下記)。手で測るための手順として
置く。進行は`SFS4_PROGRESS`が1000項目ごとにstderrへ出す。

#### 実際に通した結果 (2026-09-09)

`sh tools/fetch.sh gcc47`で取得した配布木 (SHA-256照合済み) に対して1度通した。

| | 値 |
|---|---:|
| magic | `sfs4` |
| 使用量 (cursor) | 484,378,920 bytes |
| data領域の余り | 52,491,992 bytes |
| 表の空き | 49,716 / 131,072項目 |
| 経路の集合 | 81,355行がsourceと一致 (diffは空) |
| 所要時間 | 1h27m (pack約50分・`list`による読み直し約37分) |

**使用量は`measure`の見積りと1 byteも違わなかった。** 表を先に切ってから
詰めた分だけcursorが進むという設計どおりである。深さ12・81,355経路の木を
shell実装のpackが通し、親の索引付けも読み直しで崩れていない。7.4の
workspace 4行はこれで実物の裏付けを持つ。

所要時間の内訳は、packが約1,600項目/分、`list`が使用中の項目を約38項目/秒で
読み直し、残る空き枠49,716枠を1枠ずつ走査する分が数分。読み直しは表の枠
ごとに`od`を起動する構造なので、枠数に比例する。1度通せば十分な手順であり、
CIで回す理由は無い。

### 7.6 何を検査しているか

`tests/stage017` 第5部。

| 検査 | 中身 |
|---|---|
| roundtrip | 92 bytesの名前を含む木がsfs4に載って戻る。時刻もnanosecondまで一致 |
| cap (対照) | 同じ木をsfs3は名指しで拒む —— だから世代を刻んだ |
| cap | 103 bytesは載り、104 bytesは`name too long`で拒む |
| cap | 容量の式がtools/sfs4.shとkernel25.cの実際の値を読んでいる (答の判っている小さな木で確かめる) |
| cap | 作業用の表 (131,072項目) を切った場合の使用量と余りも同じ式で出す (7.4) |
| cap | 深さ12の木 (経路131 bytes) がsfs4に載って戻る |
| run | 92 bytesの名前と131 bytesの経路を、引き・読み・作り、statする |
| run | guestが作った長い名前が、走った後のimageをhostで開いても在る |
| run | 旧世代の窓 (32 MiB) の外に置いた中身が読める |
| run | sfs3のimageは`?`で拒む |
| run | 窓を超える大きさのimageは`S`で拒む |
| run | 2^31を超える大きさも`S`で拒む —— 符号つきで比べていたら通り抜ける |

**「載る」だけでは足りない。** hostで詰められてもkernelが引けなければ意味が
ないので、長い名前は必ずguestでも引かせる。同じ理由で、窓が広がったことは
「旧世代なら届かない位置のfileが読める」という形でしか確かめない。

**全配布木の実packはこの表に無い。** 7.5のとおり1時間半かかるためCIでは
回さない。roadmap 6.1の4へ進む前に手で1度通す —— 2026-09-09に通した (7.5)。

## 8. GCC の翻訳単位を我々の器に読ませる (6.1 の 4)

tccのときと同じ測り方である (4.1) —— ソースを読ませ、**通らなかった単位と
その理由を数える**。対象は純粋なCで書かれた **libiberty (55単位) と
libcpp (15単位)**。gcc/本体はconfigureが生成するheader (tm.h・insn-*.h) を
要り、4.7.4にriscv backendが無いのでhost向けにconfigureするしかない。
まずこの2つの書庫で測る。

道具は`tools/gcc17.sh`の`configure` / `headers` / `unit` / `units` / `where`。
器は`cc15v`と`pp18`、libcは`libc21`である。

### 8.1 どう測るか

**単位の一覧を自分で選ばない。** `libiberty/Makefile.in`の
`REQUIRED_OFILES`と`libcpp/Makefile.in`の`libcpp_a_OBJS`から取る。

**どのlibcで測るかを明示する。** 既定は`libc21` —— zlib / bzip2を読んで
足した世代で、我々が実物に向けて持っているheaderはこれが全部である
([libc21.md](../stage017/libc21.md))。`STONE_GCC17_LIBC`で差し替えられる。

> **初回は`stage015/libc`で測ってしまった。** それは鎖の素の側
> (`tools/diff17.sh`の`bare`が測る器) で、`sys/`の下は`time.h`しか無い。
> 土台を`libc21`に替えると、閉包の閉じる単位は33から69へ、`.o`まで
> 通る単位は38から42へ増える。**表を読む前に、その表が何に対する表かを
> 言えなければならない。**

**どちらの系でppを回すかも明示する。** `STONE_GCC17_PP=os`で前処理を
**我々のOSの上の`pp18`**に通す (既定は裸の`pp16`)。`tools/tcc17.sh`と
同じ形で、作業用の根をsfs3で詰め、記憶像へ置いて`kernel24`を起動し、
走行後の像から`.i`を取り出す。1単位あたりQEMUの起動が1回増える。

この2つは`units`が表の先頭と`tmp/g17u/units.meta`に書き出す。
[gcc47-units.txt](../tests/stage017/expected/gcc47-units.txt)の頭にも
`#`付きで残してある —— 表だけを見ても基準を辿れるようにするためである。

**config.hはhostのautoconfに作らせる。** configureを我々のOSで回すのは
4.2の別件である。ただしhostのheaderと語長で作ると、我々に無いheaderを
「ある」と書いたconfig.hになるので2つ手当てした。

1. `CPPFLAGS`でheaderの探し道を我々のlibcだけにする。`AC_CHECK_HEADERS`は
   「そのheaderを含む試験を訳せるか」で決めるので、`HAVE_*_H`が我々の
   headerの有無を映す
2. 語長はautoconfのcache変数でRV32の値を与える。放っておくと
   `SIZEOF_LONG`が8になる

**拒んだ理由を、同じ入力をhostに読ませて分類する。** 我々のppもccも
終了コードしか言わない。tccのときの`tools/diff17.sh`と同じ手である。

| 状態 | 意味 |
|---|---|
| `ok` | `.o`ができた |
| `hdr` | libcにheaderが無い。**空の代役で埋めて先へ進め**、その先の結果を`->`で繋ぐ |
| `ppext` | 我々のppが拒み、hostのcppも`-pedantic-errors`で拒む。規格の外の形 |
| `pp` | 我々のppが拒み、hostは通す。**我々のppの穴** |
| `decl` | ppは通るがhostのgnu89が拒む。宣言か型がlibcに無い |
| `ext` | 我々のccが拒み、hostもC89として拒む。GNU / C99の拡張 |
| `gap` | 我々のccだけが拒む。**我々のC89適合の穴** |
| `cap` | 器の上限 (ppの束ね64員 / 256員・pp/ccの6) |
| `run` | OS側のppで走行そのものが立ち上がらなかった。適合の話ではない |

我々のheader自身が出すISO診断 (`long long`) はbaselineとして引く。

### 8.2 測った結果

3回測った。**1回に1つずつしか動かさない** —— 動いた単位の数が、その1つの
効果そのものになる。

| 結果 | 1. `pp16`+`cc15v` | 2. `pp18`+`cc15v` | 3. `pp18`+`cc15w` |
|---|---:|---:|---:|
| `ok` | **42** | **42** | **48** |
| `gap` | 9 | 19 | 13 |
| `decl` | 5 | 6 | 6 |
| `ppext` | 13 | 0 | 0 |
| `ext` | 0 | 1 | 1 |
| `hdr` | 1 | 1 | 1 |
| `cap` | 1 | 1 | 1 |

**1→2 で動いたのは13単位ちょうどで、8.4の`ppext`と一致する。** 他の57単位は
状態も詳細も1文字も動いていない —— pp18が変えたのは変えるつもりだったもの
だけである。状態が同じというだけでは足りないので、`md5` / `crc32` /
`symtab`の3単位では**同じ束ねを両方のppに通して`.i`をバイト単位で比べた**
(8.4)。

**ただし`ok`は42のまま増えなかった。** 13単位は前処理を抜けただけで、
その先の壁に当たっている。

| 移った先 | 単位 | 原因 |
|---|---:|---|
| `gap 1` | 10 | 8.3の4 (`obstack.h`の`void *(*) (long)`)。既知 |
| `decl` | 2 | `struct stat`の`st_mode` / `st_mtime` (8.5) |
| `ext` | 1 | `offsetof`が定数式でない (8.6の2。新規) |

**2→3 で動いたのは6単位。** 8.3の4を[cc15w](../stage015/cc15w.md)で通した
結果である。`directives` / `directives-only` / `errors` / `expr` / `pch` /
`traditional`が`.o`まで出た。**1つ通すと次が見える** —— 残る4単位
(`charset` / `init` / `lex` / `symtab`) はobstackの原型を抜けた先で
別の壁に当たり、8.3に3つの形が足された (9・10・11)。

最大は`cp-demangle`の140,800 bytesである。headerの閉包は69単位で閉じ、
足りないのは`sys/times.h` 1つだけ (`getruntime`) である。

単位ごとの結果は[gcc47-units.txt](../tests/stage017/expected/gcc47-units.txt)、
headerの閉包は[gcc47-headers.txt](../tests/stage017/expected/gcc47-headers.txt)
にある。**これは期待値ではなく測定値である** ——
`gcc47-tree.txt` (4.5) と違い、検査が突き合わせる相手ではない。器を直せば
変わるべき値なので、次に測ったときの比較対象として置く。

### 8.3 C適合の穴 —— 11つ (`gap` の13単位)

`gcc17.sh where`が、ccが落ちる最初の関数の塊まで絞る。そこから最小の形を
作ってccに食わせた。**どれもhostはC89として通す。**

| | 形 | cc | 出た単位 |
|---|---|---:|---|
| 1 | block内の`struct tag ;` (tagだけの宣言) | 1 | concat |
| 2 | bit-field memberへの`++` | 5 | fibheap |
| 3 | 関数名を括弧で囲む定義・宣言 `int (f) (int x)` | 1 | hashtab |
| 4 | ~~prototypeの仮引数に関数pointerの抽象宣言子 `void *(*)(long)`~~ | 1 | **[cc15w](../stage015/cc15w.md)で通した** |
| 5 | block scopeの`typedef` | 1 | sort |
| 6 | block scopeの`extern`宣言 | 1 | xmalloc |
| 7 | 関数pointerの配列 `void (*fns[32]) (void)` | 1 | xatexit |
| 8 | `putc`がstdio.hに無い | 5 | mkdeps |
| 9 | 関数pointer型へのcast `(void *(*) (long)) xmalloc` | 1 | symtab, obstack, init |
| 10 | 配列の大きさの定数式に`sizeof` | 1 | lex |
| 11 | pointerの型修飾子 `int (*const f)(int)` | 1 | charset |
| 12 | 条件式の第2項が空pointer定数 `(k ? 0 : p)->f` | 5 | line-map |

1は`ansidecl.h`の`VA_OPEN`が`{ va_list ap; va_start(ap, v); { struct Qdmy`
と展開する形で、**可変長引数を使う単位すべてに効く**。

8だけは言語ではなくlibcの穴である。我々は`fputc`と`putchar`を持つが
`putc`を持たず、鎖の前置部が**1引数の**`putc`を提供しているので、
C89の2引数の呼出しが引数個数の不一致 (5) になる。

**族を振る舞いではなく言語の規則で切る**
([stage017-cc.md](stage017-cc.md) 33章の反省)。4・9・11はどれも
「抽象宣言子・型修飾子を読む場所が足りない」という近い話だが、
**通る道が別**である —— 4は仮引数並びの宣言子、9はcastの型名、
11はpointerの後ろの修飾子で、直す場所が違う。まとめて1つと数えると
「1つ直したのに単位が動かない」が起きる。

#### 4 —— 12単位を塞いでいた1つ

`include/obstack.h` 193行の

```c
extern int _obstack_begin (struct obstack *, int, int,
                           void *(*) (long), void (*) (void *));
```

をlibcppの全単位が`include/symtab.h` 22行経由で読む。`gcc17.sh where`が
`charset` / `directives` / `directives-only` / `errors` / `expr` /
`init` / `lex` / `line-map` / `pch` / `traditional`のどれでも
**`.i`の同じ1843〜1989行**を指した。pp18で前処理を通すまで、この10単位は
`ppext`で止まっていて**この壁は見えていなかった**。

[cc15w](../stage015/cc15w.md)で通した。**効いたのは6単位** ——
`.o`まで出たのは`directives` / `directives-only` / `errors` / `expr` /
`pch` / `traditional`で、残る4単位はその先の9・10・11に当たった。
**1つ通すと次が見える。**

`gcc17.sh where`で1単位ずつ絞った先は次のとおり。

| 単位 | 当たった形 | `.i`の行 |
|---|---|---|
| `symtab` | 9 (castの型名) | 2069〜2091 |
| `init` | 9 (castの型名) | 4134〜4228 |
| `obstack` | 9 (castの型名) | 762〜926 |
| `charset` | 11 (pointerの型修飾子) | 4284〜4325 |
| `lex` | 10 (`sizeof`を定数式に) | 4052〜4081 |
| `line-map` | 12 (条件式の型) | 4096〜4193 |

#### 9・10・11・12 —— 4を通して見えた4つ

いずれもcc15wで測り直して初めて出た。最小の形はどれもhostの
`gcc -std=c89 -pedantic-errors`が通す。

```c
/*  9  symtab.c 65行。仮引数並びではなく cast の型名の側である */
_obstack_begin (&table->stack, 0, 0,
                (void *(*) (long)) xmalloc,
                (void (*) (void *)) free);

/* 10  lex.c 133行。翻訳時の表明。cofs が sizeof を「会った例が無い」と
 *     して保留していたもの (stage015/cc15v.sc の註)。会った */
typedef char check_word_type_size
  [(sizeof(word_type) == 8 || sizeof(word_type) == 4) * 2 - 1];

/* 11  charset.c 455行。C89 6.5.4.1 の pointer は
 *     「* type-qualifier-list_opt」である */
static unsigned char
conversion_loop (int (*const one_conversion)(iconv_t, ...), ...)
```

11は仮引数だけの話ではない。`int (*const f)(int) = 0;`という局所の宣言も
同じく1で止まる。

12は`include/line-map.h` 518行の

```c
#define INCLUDED_FROM(SET, MAP)						\
  ((linemap_check_ordinary (MAP)->d.ordinary.included_from == -1)	\
   ? NULL								\
   : (&LINEMAPS_ORDINARY_MAPS (SET)[(MAP)->d.ordinary.included_from]))
```

を`line-map.c` 266行が`ORDINARY_MAP_INCLUDER_FILE_INDEX (INCLUDED_FROM
(set, map - 1))`のように**そのまま`->`で辿る**形である。**我々は条件式の型を
第2項から取っている**ので、第2項が空pointer定数のときに結果が`int`に
なり、`->`が型の誤り (5) になる。C89 6.3.15は

> 一方の被演算子が空ポインタ定数であれば、結果は他方の型を持つ。

と書いている。**順序に依存しない規則である。**

| 形 | cc15w |
|---|---|
| `(k ? 0 : p)->f` | **5** |
| `(k ? p : 0)->f` | 0 |
| `(k ? (struct m *)0 : p)->f` | 0 |
| `q = k ? 0 : p; q->f` | 0 |

**代入や返却では表に出ない** —— そちらは左辺の型へ変換する道を通るので、
条件式の型が誤っていても結果が合う。`->`を直に当てて初めて出る。

### 8.4 前処理器 —— `defined` がmacro展開で現れる (`ppext` の13単位)

**libcppの13単位すべてが同じ1つの形で止まる。** `system.h` 379行の

```c
#define HAVE_DESIGNATED_INITIALIZERS \
  (!defined(__cplusplus) \
   && ((GCC_VERSION >= 2007) || (__STDC_VERSION__ >= 199901L)))
```

を`internal.h` 577行が`#if`で使う。**macro展開の結果に`defined`が現れる
形はC89 6.8.1で未定義動作**であり、hostの`gcc -std=c89 -pedantic-errors`も
`this use of "defined" may not be portable`で拒む。我々のppは4 (条件指令の
誤り) で止まる。

したがってこれは我々の穴ではない。**ただしGCCを組むには通す必要がある** ——
GCCは自分自身がこの形を受けることを前提に書かれている。

**通す側に倒した。[pp18](../stage017/pp18.md) である。** ホストのcppも
clangも評価する側に倒しており、倒す先は1つしかない。pp17の全文複製に、
`#if`の式を読むところだけを足した —— `dodefined()`の規定 (展開より先に潰す)
も、展開の仕組みも、出力の1バイトも変えていない。変わるのは「展開の後に
`defined`が残っていたとき、4で止まるか値を出すか」だけである。

**限界を1つ書いておく。** `defined`のoperandが定義済みマクロのときは、
展開のときにoperand自身が展開されて名前が消えるので、pp18も4で拒む
(黙って違う答は出さない)。**この限界はGCC 4.7.4には当たらない** ——
libiberty / libcppの閉包でマクロ本体に`defined`が現れるのは`system.h`の
1箇所だけで、そのoperandは`__cplusplus`、我々のppが定義しない名前である。

**測り直した。** harnessのppの段をOS側へ移し (`STONE_GCC17_PP=os`。8.1)、
`pp18`で70単位を通し直した。**`ppext`は13から0になり、動いたのはその13単位
だけである** —— 残る57単位は状態も詳細も1文字も変わっていない。移った先は
8.2の表のとおりで、**10単位が8.3の4に集まった**。

状態が同じというだけでは足りないので、`md5` / `crc32` / `symtab`の3単位で
**同じ束ねを`pp16`と`pp18`の両方に通して`.i`をバイト単位で比べた** ——
20,290 / 10,495 / 24,600バイトすべて一致する。

`ppext`という状態そのものは残す。規格の外の形は`defined`だけではないので、
次にgcc/本体を測るときにまた出る。

### 8.5 libcの穴 —— 4つ (`decl` の6単位)

`libc21`は`sys/types.h`・`sys/stat.h`・`signal.h`・`dirent.h`を既に持ち、
`off_t`も`struct stat`も定義している。残るのは中身である。

| 要るもの | 単位 |
|---|---|
| `struct stat`のmember —— `st_dev` / `st_ino` / `st_mode` | fdmatch, getpwd, unlink-if-ordinary, **libcpp/files** |
| `struct stat`のmember —— `st_mtime` | **libcpp/macro** |
| `sys/times.h`と`struct tms` | getruntime |
| `_PC_PATH_MAX` (`pathconf`) | lrealpath |

我々の`struct stat`は`st_size` / `st_mtlo` / `st_mthi` / `st_type`の4つで、
sfsが持つ情報に合わせてある。`st_ino`はsfsの表の索引がそのまま使え、
`st_mode`は`st_type`から作れるが、`st_dev`は**sfsに対応するものが無い** ——
「同じファイルか」を`(st_dev, st_ino)`の組で見るソースに何を返すかは、
足すときに決める。

`st_mtime`はpp18で測り直して出た。**値は既に持っている** ——
`st_mtlo` / `st_mthi`が秒とナノ秒の64 bitを2語で運んでいる (第4部の1)。
POSIXの`time_t st_mtime`を足すかどうかは、`time_t`を32 bitにするか
64 bitにするかと同じ判断になるので、そこで決める。

### 8.6 器の上限 (`cap` の1単位) と定数式

1つめは`libiberty/regex.c`が`pp 6` (容量超過) で止まる。8,000行あり、
自分自身を2度includeして`re_search`の族をwide版まで作る形である。tccの
ときに広げたpp16の器 (入力4 MiB・macro 4096) を超える。**pp18でも同じ**
—— pp17系はアリーナが64 KiBで束ねの員も256だが、それでも足りない。

2つめはpp18で測り直して出た`ext`の1単位である。`libcpp/identifiers.c`
113行が

```c
extern char proxy_assertion_broken[offsetof (struct cpp_hashnode, ident) == 0 ? 1 : -1];
```

で翻訳時の表明を書く。我々の`offsetof`は`stddef.h` 22行の

```c
#define offsetof(t, m) ((size_t)&(((t *)0)->m))
```

で、**C89の整数定数式ではない**。6.4は整数定数式のキャストを「算術型を
整数型へ変換するもの」に限っており、アドレスからのキャストは入らない。
配列の大きさに使えないので、我々のccもhostの
`-std=c89 -pedantic-errors`も拒む。GCC自身の`stddef.h`は
`__builtin_offsetof`を使っており、**これはheaderでは閉じない** ——
器の側に組み込みが要る。

### 8.7 次の手

埋める順番は、**塞いでいる単位の数**で決まる。

1. ~~8.4の`defined` —— libcpp 13単位が一斉に動く~~
   **[pp18](../stage017/pp18.md)で通した。**
2. ~~harnessのppの段をOS側へ移す~~
   **`STONE_GCC17_PP=os`で移した (8.1)。13単位を測り直した結果が8.2である。**
3. ~~8.3の4 (`void *(*)(long)`) —— 12単位を塞いでいる~~
   **[cc15w](../stage015/cc15w.md)で通した。6単位が`.o`まで出た (8.2)。**
4. **8.3の9 (関数pointer型へのcast) —— 3単位。** 4を通した先に出た形で、
   `symtab` / `init` / `obstack`が待っている
5. 8.3の10 (`sizeof`を定数式に)・11 (pointerの型修飾子)・12 (条件式の型)
   —— 各1単位。10は`cofs`が「会った例が無い」として保留していたもので、
   会った。12は**規則を取り違えていた**もので、他の11と性質が違う
6. 8.3の1 (`struct tag ;`) —— 可変長引数を使う単位すべてに効く
7. 8.3の残り5つ (2・3・5・6・7) と8 (`putc`)
8. 8.5の`struct stat`のmember (5単位)・`sys/times.h`・`_PC_PATH_MAX`
9. 8.6の2 (`offsetof`が定数式) —— headerでは閉じず、器に組み込みが要る

**順番は測るたびに入れ替わる。** 1を通したら「いちばん効く1つ」が8.4から
8.3の4へ移り、その4を通したら残った4単位が9・10・11へ散った。**塞いでいる
単位の数は、その前の壁を通すまで判らない。**

**gcc/本体 (362単位) はまだ測っていない。** 生成header (`tm.h`・
`insn-*.h`) とhost向けconfigureが要る。4〜6を埋めてから同じharnessで測る。
