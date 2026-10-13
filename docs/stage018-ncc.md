# Stage 18: psABI の処理系 (ncc1・nas1・nld1) 設計文書

## 0. 本書の対象

Linux 5.4 を訳すために新しく書く処理系の設計を記録する。対象は次の 3 本と，それを Kbuild から呼ぶための周辺の道具である。名前は仮であり，世代の番号は既存の系統 (cc15xx・ld18・ar17) と分ける。

| 名前 | 役割 | 入力 → 出力 |
|---|---|---|
| ncc1 | C コンパイラ | 前処理済みの C (`.i`) → GNU as 書式のアセンブリ (`.s`) |
| nas1 | アセンブラ | `.s` (`.S` は前処理した後) → ELF32 再配置可能オブジェクト (`ET_REL`) |
| nld1 | リンカ | `ET_REL`・書庫・リンカ台本 → `ET_EXEC` / `ET_DYN` / `ET_REL` (`-r`) |
| 周辺 | ドライバ `ncc`，`objcopy`・`nm`・`objdump -h` の部分集合，ar17 の拡張 | Kbuild が呼ぶ形を受ける |

前処理器 (`-I` の探索順・`-MMD`・`__COUNTER__` を持つ新しい pp の世代) と，OS・libc・sh・mk の拡張は別の文書で扱う。本書では ncc1 から見た前処理器との境界だけを決める (8 章)。

[stage019-linux.md](stage019-linux.md) 2 章の決定 (stone 製の意味，ホストからの測定) と，[plan.md](plan.md) 2.1 / 2.2 の純度・検証ポリシーに従う。

## 1. 決めたこと (2026-10-13)

| 項目 | 決定 |
|---|---|
| 方式 | 新しい C コンパイラを C で書き，cc15as で組む。cc15as は拡張しない (2026-10-13 の検討で案 B として選んだもの) |
| 出力 | ncc1 はアセンブリの文字列を出す。機械語とオブジェクトの組立ては nas1 が，番地の確定は nld1 が受け持つ |
| 呼出し規約 | RISC-V psABI の ilp32 (浮動小数点レジスタを使わない)。cc15as の独自規約とは混ぜない |
| 対象の命令 | RV32IMA + Zicsr + Zifencei。ncc1 が生成するのは RV32IM だけで，A と CSR は inline asm と `.S` を通して nas1 が扱う。圧縮命令 (C) は生成も受理もしない |
| 書く言語 | C89 に，cc15as が受ける範囲の `long long` を足したもの。ホストの gcc でも組めるように書く (5.1) |

## 2. 完了条件

1. rv32-stone の C の単位 374 個すべてを ncc1 が訳し，nas1 が `.S` の単位 15 個と ncc1 の出力すべてを組み立てる
2. nld1 が `vmlinux.lds` で `vmlinux` を，`vdso.lds` で `vdso.so` を組む。`ld -r` による `vmlinux.o` を modpost が読める
3. その `vmlinux` が QEMU virt (rv32) で起動し，我々の SBI の上で initramfs の init まで届く (stage019-linux.md の完了条件 2 と同じもの)
4. 7 章の試験がすべて通る

1〜3 はホストから駆動して確かめてよい (stage019-linux.md 2 章の「開発中の測定」)。stone の OS の中で全体を回すことは Stage 19 の完了条件として扱う。

## 3. 構成と流れ

```
.c --pp--> .i --ncc1--> .s --nas1--> .o --+
.S --pp (-D__ASSEMBLY__)--> .s --nas1--> .o --+--> nld1 (+ .lds) --> vmlinux / vdso.so / vmlinux.o
                                    ar (thin) --> built-in.a --+
```

ドライバ `ncc` は gcc の命令行を受け，`-E` は pp，`-S` は pp + ncc1，`-c` は pp + ncc1 + nas1 (`.S` は pp + nas1)，それ以外 (`-shared`・`-nostdlib`・`-Wl,…`) は nld1 を呼ぶ。Kbuild の問合せ (`--version`・`-dumpversion`・`-print-file-name`) と cc-option の試しに，GCC 4.6.4 として答える。

段を分ける理由は 3 つある。

1. Linux の `.S` 15 単位と inline asm の文字列は GNU as の書式で書かれており，アセンブラはどのみち要る。ncc1 も同じ書式を出せば，機械語の生成は nas1 の 1 箇所に集まる
2. inline asm は文字列をアセンブリへ埋めるだけでよくなる。ncc1 は命令を解釈しない
3. 段ごとの出力がテキストまたは標準の ELF なので，verify 層の `objdump` / `readelf` と目で検査できる

## 4. ncc1

### 4.1 段の構成

1. **字句解析**: 翻訳単位全体を 1 度だけ字句に分け，識別子をハッシュ表で intern する。pp の行番号の印 (`# <n> "<file>"`) を読み，診断に使う
2. **構文解析と型付け**: 再帰下降で読み，型の付いた抽象構文木 (AST) を作る。宣言は読みながら記号表へ入れる
3. **定数の畳込み**: 整数定数式 (ICE) と番地定数を評価する。初期化子・`_Static_assert`・`__builtin_constant_p`・`__builtin_choose_expr`・制御の除去 (4.3) が使う
4. **コード生成**: 関数ごとに AST を辿ってアセンブリを出す。中間表現は置かない (4.4)
5. **出力の選別**: 参照された `static` 関数・変数だけを出す (4.3)

関数の本体の AST は，その関数を出し終えたら領域ごと捨てる。前処理済みの単位は平均 1.08 MB (374 単位で 404 MB。最大 2.9 MB) あり，大半は header の宣言と `static inline` 関数である。

### 4.2 言語の範囲

stage019-linux.md 6 章で測ったものすべてと，C89 のすべてを持つ。加えて，字面の計数に入れていなかったが Linux が使う次を持つ (`linux19.sh scan` に足して数え直す)。

- `_Bool`，複合リテラル `(T){ … }`，無名の構造体・共用体の成員，柔軟配列成員，`__func__`，文の後の宣言
- `__attribute__((mode(QI/HI/SI/DI)))`，`INT_MAX` を超える列挙子 (GCC と同じく型を広げる)
- 大域の `asm("…")` (そのまま出力へ写す)

浮動小数点の演算と定数は生成しない (カーネルは soft-float で浮動小数点を使わない)。出会えば診断して止まる。計算した goto と asm goto は数えて 0 だったので持たない。

### 4.3 最適化をしないことと，しなければならない除去

ncc1 は最適化をしない。ただし Linux は次の 2 つを前提にしており，これを欠くとリンクで未定義の名前が残る。これは最適化ではなく意味の一部として実装する。

**定数条件の枝の除去。** 条件が ICE に畳み込める `if`・`?:`・`&&`・`||`・`switch` (定数の選択式) は，選ばれない側のコードを出さない。`if (IS_ENABLED(CONFIG_X)) f();` の `f` が組まれていない場合や，`switch (sizeof(*p))` の `default: __bad_size_call_parameter();` がこれに当たる。選ばれない側の中のラベルへ外から跳ぶ場合だけは除去しない (C の意味を保つ)。

**参照されない `static` の省略。** header の `static inline` 関数は単位ごとに数百あり，組まれていない関数を呼ぶものも含む。`static` の関数と変数は，外部に見える定義・`used` 属性付きのもの・大域の asm から参照されたものを根として，到達したものだけを出す。本体の字句の範囲を覚えておき，参照された時点で解析する (解析は出力の直前にまとめて行う)。

`__compiletime_assert` (BUILD_BUG_ON) は，条件が ICE でない場合に `error` 属性の関数の呼出しが残る。GCC はこれを inline 展開の後に判定するので，**我々が inline 展開をしないことによる差はここに出る**。呼出しが除去されずに残った `error` 属性の関数は，GCC と同じく翻訳時の誤りとして報告し，件数を測る。多ければ `always_inline` の関数に限った展開を足す (11 章の危険 1)。

### 4.4 コード生成とレジスタの割当て

中間表現を置かず，型付きの AST から直接アセンブリを出す。理由は次のとおりである。

- 完了条件に生成コードの速さが無い。カーネルの性能は起動試験で問題にならない
- ncc1 自身の速さは，翻訳する入力の量 (404 MB) に比例する字句解析と構文解析が支配する
- 生成の誤りを AST の 1 節点と出力の数行に対応付けて追える

割当ては次のとおりとする。

| 対象 | 置き場所 |
|---|---|
| 局所変数・仮引数 | フレームの枠。仮引数は入口でレジスタから枠へ移す |
| 式の一時値 | t0〜t6 と s1〜s11 の溜まりから深さ順に取る。s を使った関数は prologue で退避する。溜まりが尽きれば枠へ退避する |
| 呼出しをまたぐ一時値 | 呼出しの前に生きている caller-saved の一時値を枠へ退避し，後で戻す |
| 64 bit の値 | 一時値 2 個 (下位・上位) |
| フレーム | s0 を常にフレームポインタとする。sp は 16 バイトに揃える |

後の改善として，番地を取られない scalar の局所変数を s レジスタへ置くことを考える (ncc1 を自分自身で組む場合に効く。9 章)。生成の正しさを確かめた後に行う。

### 4.5 呼出し規約 (psABI ilp32)

[RISC-V ELF psABI](#一次資料) の整数の呼出し規約に従う。要点は次のとおりで，実装の前に一次資料の文面で照合する。

- 引数は a0〜a7。XLEN (32 bit) 以下の scalar は 1 本，2×XLEN の scalar は 2 本 (下位が若い番号)。1 本しか残らなければ下位をレジスタ，上位をスタックに置く
- 可変長引数のうち 2×XLEN の整列を持つものは，偶数番から始まる対で渡す (固定の引数にはこの整列は無い)
- 2×XLEN 以下の構造体は scalar と同じくレジスタで，それを超えるものは呼ぶ側の写しへの番地で渡す
- 戻り値は a0 (/ a1)。2×XLEN を超えるものは呼ぶ側が領域を用意し，その番地を a0 に隠れた第 1 引数として渡す
- callee-saved は s0〜s11 と sp。gp・tp は割り当てない
- `va_list` は `void *`。可変長の関数は a0〜a7 をスタック引数の直前の領域へ退避し，スタック引数と連続させる

64 bit の除算・剰余だけは実行時支援 (`__divdi3` など) の呼出しにし，乗算・比較・シフトはその場で展開する。カーネルは除算の支援を持たず，GCC が除算以外を展開することを前提にしている (riscv の 5.4 が選ぶ支援は `GENERIC_LIB_ASHLDI3`・`ASHRDI3`・`LSHRDI3`・`UCMPDI2` だけである)。

### 4.6 code model

| | 大域の番地 | 関数の呼出し |
|---|---|---|
| `medlow` (既定) | `lui %hi(s)` + `addi %lo(s)` | `call s` (`auipc` + `jalr`。R_RISCV_CALL) |
| `medany` (`-mcmodel=medany`) | `lla` (`auipc %pcrel_hi` + `addi %pcrel_lo`) | 同上 |

`-mcmodel=medany` は単位ごとの選択肢として ncc1 に渡り，`arch/riscv/mm/init.o` だけが使う。

### 4.7 inline asm

ncc1 は命令を解釈せず，`%n`・`%[name]`・`%z`・`%%` を置き換えた文字列を出す。制約は測った 11 種を持つ。

| 制約 | 扱い |
|---|---|
| `r`・`=r`・`+r` | 一時値のレジスタ。出力は asm の後で左辺値へ書き戻す |
| `=&r` | 入力に使ったレジスタと別のものを取る |
| `rJ`・`rK` | 定数 0 なら `zero` (`J`)，5 bit の符号なし定数ならその値 (`K`)，それ以外はレジスタ |
| `i` | ICE の値を 10 進で出す。asm-offsets (`DEFINE`) はこれで定数を取り出す |
| `m`・`=m`・`+rm` | 番地をレジスタへ求め `0(reg)` と書く |
| `+A` | 番地をレジスタへ求め `(reg)` と書く (AMO の番地) |

clobber に書かれた callee-saved レジスタは prologue で退避する。`memory` の clobber は扱いを変えない (値をレジスタに持ち越さないため)。

**レジスタを指定した変数** (`register T x asm("a0")`) は 2 通りに分ける。

- `sp`・`gp`・`tp`: 枠を持たず，読むたびにそのレジスタから `mv` する (`get_current()` が `tp` を読む)
- それ以外 (`a0`〜`a7` など): 普通の局所変数として枠に置き，asm の被演算子に使われた時だけ指定のレジスタへ載せ，asm の後で書き戻す。GCC の文書が保証するのもこの用途だけである

### 4.8 組込み関数と属性

| 組込み | 扱い |
|---|---|
| `__builtin_constant_p(e)` | `e` が ICE なら 1，それ以外は 0。`e` は評価しない |
| `__builtin_expect(e, c)`・`__builtin_extract_return_addr(e)` | `e` |
| `__builtin_choose_expr`・`__builtin_types_compatible_p`・`__builtin_offsetof` | 翻訳時に決める |
| `__builtin_unreachable()` | 命令を出さない |
| `__builtin_memcpy` | `memcpy` の呼出し |
| `__builtin_object_size(p, t)` | `t` が 0 / 1 なら `(size_t)-1`，2 / 3 なら 0 (大きさが分からない時の GCC の値) |
| `__builtin_va_*` | 4.5 の `va_list` で展開する |
| `__builtin_return_address(0)` | 枠に退避した ra。0 以外の段数は 0 |
| `__builtin_prefetch` | 引数を評価するだけ |
| `__builtin_clz*` などのビット演算 | その場で展開する (カーネルは `__clzdi2` を持たない) |

属性のうち生成に効くのは `aligned`・`packed`・`section`・`weak`・`alias`・`used`・`unused` (出力の選別)・`mode`・`gnu_inline` (外部定義を出さない)・`error` (4.3)・`noreturn` (呼出しの後に何も出さなくてよい) である。`always_inline`・`noinline`・`cold`・`const`・`pure`・`malloc`・`format`・`nonnull`・`assume_aligned`・`designated_init`・`externally_visible`・`no_instrument_function` は受けて無視する。

### 4.9 出力の書式

- 節: `.text`・`.data`・`.rodata`・`.bss`，属性の `section` は `.section <名前>,"<旗>",@progbits|@nobits`。旗は GCC と同じく，関数なら `ax`，書換えのできる変数なら `aw`，`const` なら `a`，名前が `.bss` で始まれば `@nobits` とする
- 記号: `.globl`・`.weak`・`.type`・`.size`・`.set` (`alias`)。仮定義は `-fno-common` (Kbuild の指定) として `.bss` に定義する
- 局所ラベルは `.L` で始め，nas1 が記号表に入れない

## 5. ncc1 自体の作り

### 5.1 2 通りに組む

ncc1 は cc15as + ld18 で組んで stone の OS で走らせるものと，ホストの gcc で組んでホストで走らせるものの 2 通りを作る。kconf1 と同じ方式である。

- ホスト版は開発中の測定 (374 単位を並列に訳す) に使う。cc15as で組んだ ncc1 を QEMU で走らせると単位あたり数分かかるからである
- 両者の出力 (`.s`) が同じであることを試験で確かめる (7 章の 5)。ncc1 の中で対象の値を表すときは幅を固定した型 (`long long` の 64 bit) を使い，ホストの `long` の幅に依らないようにする

### 5.2 規模の見積り

| 部分 | 行数 (目安) |
|---|---|
| 字句解析・記号表・診断 | 1,500 |
| 構文解析・型・宣言 | 5,000 |
| 定数の畳込み・初期化子 | 1,500 |
| コード生成 (psABI・64 bit・構造体) | 4,000 |
| inline asm・組込み関数・属性 | 1,200 |
| 計 | 約 13,000 |

## 6. nas1 と nld1

### 6.1 nas1

- **命令**: RV32IMA，Zicsr (CSR の名前を含む)，Zifencei，`sfence.vma`・`wfi`・`sret`・`mret`。疑似命令は `li`・`la`・`lla`・`call`・`tail`・`mv`・`not`・`neg`・`seqz`・`snez`・`j`・`jr`・`ret`・`nop`・分岐の別名・`csrr` / `csrw` / `csrs` / `csrc` とその即値形
- **式と再配置**: `%hi`・`%lo`・`%pcrel_hi`・`%pcrel_lo` (ラベルを引く)，記号 ± 定数，同じ節の中のラベルの差。出す再配置は R_RISCV_32・HI20・LO12_I・LO12_S・PCREL_HI20・PCREL_LO12_I・PCREL_LO12_S・CALL・JAL・BRANCH。`R_RISCV_RELAX` は出さず，`.option norelax` / `push` / `pop` は受けて無視する
- **擬似命令**: 測った `.option`・`.text`・`.data`・`.rodata`・`.section`・`.previous`・`.pushsection`・`.popsection`・`.balign`・`.align`・`.word`・`.long`・`.dword`・`.quad`・`.ascii`・`.globl`・`.global`・`.set`・`.incbin`・`.macro` / `.endm`・`.altmacro` と，ncc1 が出すもの (4.9)。`.cfi_*` は受けて出力しない (Kbuild は `-fno-asynchronous-unwind-tables` を渡す)
- **数字のラベル** `1:` / `1b` / `1f`
- **2 パス**: 緩和をせず圧縮命令も無いので，命令の大きさは 1 パス目で決まる (`li` は即値で 1 命令か 2 命令かが決まる。前方の `.set` の値を要る `li` は誤りとする)
- **出力**: ELF32 `ET_REL`，`EM_RISCV`，`e_flags` は soft-float の ilp32，`.rela.*`・`.symtab`・`.strtab`・`.shstrtab`

### 6.2 nld1

Kbuild の記録 (rv32-stone) で呼ばれる形を持つ。

| 形 | 使う所 |
|---|---|
| `-T <台本>`・`--whole-archive`・`--start-group` / `--end-group` | `vmlinux` |
| `-r` | `vmlinux.o` (modpost が読む)，`lib-ksyms.o` (`EXTERN` だけの台本) |
| `-r --just-symbols` | `vdso-syms.o` (vdso の記号の番地だけを持つオブジェクト) |
| `-shared -soname --hash-style=both --build-id` | `vdso.so` (`ET_DYN`。`.dynsym`・`.dynstr`・`.hash`・`.gnu.hash`・`.dynamic`) |
| `--build-id` | `.note.gnu.build-id`。SHA-1 で計算する |
| thin archive と記号表付きの書庫 | `built-in.a` (`ar cDPrST`)，`lib.a` (`ar cDPrsT`) |

リンカ台本は `vmlinux.lds` と `vdso.lds` を前処理したものが使う部分集合を持つ。`SECTIONS`・出力節の番地と `AT()`・`ALIGN()`・`.` への代入・記号の代入・`PROVIDE`・`KEEP`・`SORT`・ワイルドカードの入力節・`/DISCARD/`・`OUTPUT_ARCH`・`ENTRY`・`PHDRS`・`ASSERT`・`ADDR` / `LOADADDR` / `SIZEOF`・`EXTERN` が候補である。実際に使う関数と文の一覧は，前処理した 2 本の台本から数えて決める (10 章の M5 の最初の作業)。

緩和はしない。vdso の 5 単位はすべて `.S` で，位置独立でない C を含まないので，`-shared` で動的再配置は生じない。

### 6.3 周辺の道具

| 道具 | 要る形 |
|---|---|
| `objcopy` | `-O binary`・`-j <節>`・`-G <記号>` (指定以外の大域記号を局所にする)・`-S` |
| `nm` | 既定の書式と `-n` (`System.map` を作る `scripts/mksysmap`) |
| `objdump` | `-h` (`lib-ksyms` の台本を作る sed が節の名前を拾う) |
| `ar` | ar17 に `T` (thin)・`D`・`P`・`s` / `S` を足す |
| `ncc` | 3 章のドライバ |

## 7. 検証

[plan.md](plan.md) 2.1 により，既存の as / ld は照合の相手にも使わない。照合は次の手段で行う。

1. **nas1 の符号化**: すべての命令と疑似命令を並べた `.s` を組み立て，verify 層の `objdump -d -M no-aliases` で逆アセンブルした結果を期待の列と突き合わせる (plan.md 2.2 の用途の内)。ELF の構造は `readelf -a` で検査する
2. **ncc1 の意味**: 試験の C を ncc1 + nas1 + nld1 で組み，QEMU virt の上で直接走らせる (stone の OS も Linux も介さない)。起動の数行と UART への出力だけを持つ素の実行環境を `.S` と C で用意する。期待値は，同じ C をホストの gcc で組んで走らせた出力とする (stage017-gcc.md 8.8 の差分試験と同じく，幅を固定した型で書いて ILP32 と LP64 の差を避ける)
3. **psABI**: `.S` の呼ぶ側 / 呼ばれる側と C の間で，4.5 の各項 (64 bit の分割・可変長の偶数番・構造体の大きさの境目・隠れた戻り値の番地) を組合せで呼び合う試験を書く。期待値は psABI の一次資料の文面から導く
4. **Linux の単位**: ホスト版 ncc1 で 374 単位を訳し，nas1 が組み立てられることを表にする (`tools/linux19.sh` を広げる)。起動 (完了条件 3) が全体の照合になる
5. **2 通りの組み方の一致**: cc15as で組んだ ncc1 とホスト版の ncc1 に同じ単位を与え，`.s` が一致することを確かめる。単位は Linux から 1 MB 級のものを数本と試験の C 全部とする

## 8. 前処理器との境界

- ncc1 は前処理済みの入力だけを読み，`#` で始まる行は行番号の印として扱う
- 処理系が前もって定義するマクロは [stage019/predef-rv32-gcc46.h](../stage019/predef-rv32-gcc46.h) の内容を pp が自分で持つ。`__OPTIMIZE__` / `__OPTIMIZE_SIZE__` は GCC の `-Os` と同じく定義する (`__compiletime_assert` が有効になり，4.3 の除去が必要になる)
- pp に要るもの (`-I` / `-isystem` の探索順，`-include`，`-Wp,-MMD,<file>`，`__COUNTER__`，`##__VA_ARGS__`) は pp の文書で扱う。それまでの測定はホストの cpp で前処理する (stage019-linux.md 6 章の方式)

## 9. 範囲に入れないもの

- 最適化 (4.3 の除去を除く)。ncc1 を自分自身で組む自己ホストは完了条件に入れない。自己ホストには psABI の libc と，stone の OS が ELF の実行形式を読むことが要る。Stage 19 の全体を stone の中で回した時間を測ってから要否を決める
- 浮動小数点，計算した goto，asm goto，可変長の配列 (rv32-stone で使われるかは scan で確かめる)
- 圧縮命令と緩和
- dtc (stage019-linux.md 5.3 の決定のとおり)

## 10. 部の切り方

| 部 | 内容 | 終わりの判定 |
|---|---|---|
| M1 | nas1 | 7 章の 1 が通る。Linux の `.S` 15 単位 (ホストの cpp で前処理したもの) が組み立てられる |
| M2 | nld1 の基本 (`-T` の最小形・`ET_EXEC`) と素の実行環境 | `.S` だけの試験が QEMU で走る |
| M3 | ncc1 の C89 と psABI | 7 章の 2・3 が通る |
| M4 | ncc1 の GNU 拡張・組込み・inline asm | 374 単位すべてが訳され，nas1 を通る |
| M5 | nld1 のリンカ台本・`-r`・書庫・`-shared`，周辺の道具 | ホストから駆動した Kbuild で `vmlinux` ができる |
| M6 | SBI・initramfs の init・起動 | 完了条件 3 |

M6 までをホストから駆動して終えた後，stone の OS の中で回すための作業 (pp・OS・libc・sh・mk・bc・getconf) に移る。M1 と M3 の前半は互いに依存しないが，試験の実行に nas1 と nld1 が要るので M1・M2 を先にする。

## 11. 危険

1. **inline 展開に依る `BUILD_BUG_ON`**: 4.3 の判定で残る件数が多ければ，`always_inline` の関数に限って呼出しの場所で本体を展開する。引数の定数を伝えて畳み込むところまでで足りるかを測ってから決める
2. **psABI の誤り**: 照合の相手になる既存の処理系が無いので，C と `.S` の境目 (`entry.S` から C を呼ぶ所，`memcpy.S` など) の誤りは起動まで表に出ない。7 章の 3 を先に厚くする
3. **ncc1 の速さ**: cc15as で組んだ ncc1 で 404 MB の入力を訳す時間が，stone の中で回す段の大きさを決める。M4 の後に単位あたりの時間を測り，9 章の自己ホストの要否を判断する材料にする

## 一次資料

実装の前に取得し，[SOURCES.md](SOURCES.md) に固定して記録する (取得は承認を得てから行う)。

- RISC-V ELF psABI Specification (riscv-non-isa/riscv-elf-psabi-doc，批准版 1.0): 呼出し規約，ELF の `e_flags`，再配置の種類と計算式
- RISC-V Assembly Programmer's Manual (riscv-non-isa/riscv-asm-manual): 疑似命令，`%hi` などの修飾子，`.option`
- GNU as と GNU ld の利用者向け文書 (binutils 2.40 の版): 擬似命令とリンカ台本の文法と意味。文書だけを読み，ソースは参照しない
- 命令の符号化は既に持つ The RISC-V Instruction Set Manual Volume I / II ([SOURCES.md](SOURCES.md)) による
