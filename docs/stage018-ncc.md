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

1. rv32-stone の C の単位 374 個すべてを ncc1 が訳し，nas1 が `.S` の単位 12 個と ncc1 の出力すべてを組み立てる
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

1. Linux の `.S` の単位と inline asm の文字列は GNU as の書式で書かれており，アセンブラはどのみち要る。ncc1 も同じ書式を出せば，機械語の生成は nas1 の 1 箇所に集まる
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

**定数条件の枝の除去。** 条件が ICE に畳み込める `if`・`?:`・`&&`・`||`・`switch` (定数の選択式) は，選ばれない側のコードを出さない。`if (IS_ENABLED(CONFIG_X)) f();` の `f` が組まれていない場合や，`switch (sizeof(*p))` の `default: __bad_size_call_parameter();` がこれに当たる。`switch` では，選ばれた `case` から `break` などの移り先に届くまでの後続の文 (fallthrough) も到達する側に含める。除去するのは，選ばれた入口からも，外から跳ぶラベルからも到達しない文だけである (C の意味を保つ)。

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

[RISC-V ELF psABI](#一次資料) 1.0 の整数の呼出し規約 (`riscv-cc.adoc` の Integer Calling Convention と va_list の節) に従う。要点は次のとおりで，2026-10-14 に文面と照合した。

- 引数は a0〜a7。XLEN (32 bit) 以下の scalar は 1 本，2×XLEN の scalar は 2 本 (下位が若い番号)。1 本しか残らなければ下位をレジスタ，上位をスタックに置く
- XLEN より狭い整数の引数は，型の符号に従って 32 bit へ広げてから渡す (呼ぶ側が拡張する)
- スタックで渡す引数は，型の整列と XLEN の大きい方に揃える (64 bit の値は 8 バイト境界)。ただしスタックの整列 (16 バイト) を超えては揃えない。最初のスタック引数は入口の sp + 0 にある
- 可変長引数のうち 2×XLEN の整列を持つものは，偶数番から始まる対で渡す (固定の引数にはこの整列は無い)。対が残っていなければスタックで渡し，**それ以降の引数はすべてスタックで渡す** (a7 は空いたまま残る)。呼ぶ側はこの「スタックへ移った」状態を引数の割付けの終わりまで持ち，7 章の 3 の組合せに入れる
- 2×XLEN 以下の構造体は scalar と同じくレジスタで (メモリ上の配置のまま。1 本しか残らなければ先頭の XLEN bit をレジスタ，残りをスタック)，それを超えるものは呼ぶ側の写しへの番地で渡す。呼ばれた側はその写しを書き換えてよい
- 戻り値は，同じ型の最初の固定引数と同じ形で a0 (/ a1) に置く。番地で渡すことになる大きさのものは呼ぶ側が領域を用意し，その番地を隠れた第 1 引数として a0 で渡す。戻った時の a0 がその番地であることは前提にしない
- callee-saved は s0〜s11 と sp。gp・tp は値を変えない (割り当てない)。sp は入口で 16 バイト境界にあり，関数の中でも常に揃えておく
- ビットフィールドの配置も同じ節の規則 (小さい番地から詰め，型の整列境界をまたぐものは次の境界から始める) に従う
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
| 64 bit の値 | `r` の入力は下位の語のレジスタを渡す (GCC はレジスタの対を取り，`%N` に下位のレジスタを出す)。`rm` と `m` は記憶域にする。レジスタに載る 64 bit の出力は持たない |

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
3. **psABI**: `.S` の呼ぶ側 / 呼ばれる側と C の間で，4.5 の各項 (64 bit の分割・可変長の偶数番と，a7 だけが残った時のスタック渡しとその後の引数・構造体の大きさの境目・隠れた戻り値の番地) を組合せで呼び合う試験を書く。期待値は psABI の一次資料の文面から導く
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
| M1 | nas1 | 7 章の 1 が通る。Linux の `.S` の単位 (ホストの cpp で前処理したもの) が組み立てられる。**完了** (12 章) |
| M2 | nld1 の基本 (`-T` の最小形・`ET_EXEC`) と素の実行環境 | `.S` だけの試験が QEMU で走る。**完了** (13 章) |
| M3 | ncc1 の C89 と psABI | 7 章の 2・3 が通る。**完了** (14 章) |
| M4 | ncc1 の GNU 拡張・組込み・inline asm | 374 単位すべてが訳され，nas1 を通る。**完了** (15 章) |
| M5 | nld1 のリンカ台本・`-r`・書庫・`-shared`，周辺の道具 | ホストから駆動した Kbuild で `vmlinux` ができる |
| M6 | SBI・initramfs の init・起動 | 完了条件 3 |

M6 までをホストから駆動して終えた後，stone の OS の中で回すための作業 (pp・OS・libc・sh・mk・bc・getconf) に移る。M1 と M3 の前半は互いに依存しないが，試験の実行に nas1 と nld1 が要るので M1・M2 を先にする。

## 11. 危険

1. **inline 展開に依る `BUILD_BUG_ON`**: 4.3 の判定で残る件数が多ければ，`always_inline` の関数に限って呼出しの場所で本体を展開する。引数の定数を伝えて畳み込むところまでで足りるかを測ってから決める
2. **psABI の誤り**: 照合の相手になる既存の処理系が無いので，C と `.S` の境目 (`entry.S` から C を呼ぶ所，`memcpy.S` など) の誤りは起動まで表に出ない。7 章の 3 を先に厚くする
3. **ncc1 の速さ**: cc15as で組んだ ncc1 で 404 MB の入力を訳す時間が，stone の中で回す段の大きさを決める。M4 の後に単位あたりの時間を測り，9 章の自己ホストの要否を判断する材料にする

## 12. M1 の到達点 (2026-10-14)

nas1 ([stage018/nas1.c](../stage018/nas1.c)，約 3,100 行) を書いた。`sh tools/build.sh stage018` が pp・cc15as・ld18 と libc26 で `tmp/build/nas1` に組み，試験 ([tests/stage018/test.sh](../tests/stage018/test.sh)) はそれを kernel29 の上で走らせる。

| 検査 | 結果 |
|---|---|
| 命令の符号化 (`tests/stage018/nas1/insn.s`) | 216 命令が，ISA の文面から書いた期待と verify 層の objdump の読みで一致 ([verify/nas1insn.sh](../verify/nas1insn.sh)) |
| 節・データ・再配置・記号 (`tests/stage018/nas1/elf.s`) | 入力から手で数えた期待 (`elf.expect`) と readelf の読みで一致 ([verify/nas1elf.sh](../verify/nas1elf.sh)) |
| 誤った入力 | 範囲外の分岐・知らない命令・範囲外の即値・`.err`・知らないレジスタ・記号の二重定義の 6 種を止める |
| ホスト版との一致 | cc15as で組んで stone の OS で走らせたものと，ホストの gcc で組んだものが，試験の入力と Linux の単位のすべてで同じ `.o` を出す |
| Linux 5.4 の `.S` | rv32-stone の 12 単位のうち，ファイルから組む 10 単位をすべて組み立てる。残る 2 単位 (`lib-ksyms` の `.lib_exports.o`) は標準入力の空行を組むもので，空の入力として試した |

### 12.1 単位の数

[stage019-linux.md](stage019-linux.md) 5.2 の表の「`.S` の単位 15」は Kbuild の記録で `-D__ASSEMBLY__ -c` を持つ行の数で，`as-instr` などの試し 3 件を含む。試しを除いた単位は 12 で，うち 2 つは `echo | $(CC) ... -x assembler -` の形で標準入力を組む。

### 12.2 GNU as に合わせて足したもの

Linux の `.S` を通すために，6.1 の範囲に次を足した。いずれも GNU as が受ける形である。

- `add a3, a3, 4` のように即値を書いた R 形の命令を I 形 (`addi` など) にする
- `csrs 0x100, 2` のように即値を書いた CSR の疑似命令を即値形 (`csrrsi` など) にする
- `.type sym STT_FUNC` (カンマを省いた形)
- 解けない記号の差 (`.dword _end - _start`) を R_RISCV_ADD64 / SUB64 の対にする (psABI の `riscv-elf.adoc` の ADD / SUB の再配置)

### 12.3 試験が見つけた誤り

- `.asciz` が終端の 0 を置いていなかった (擬似命令名を 4 文字目で判定していた)。`elf.expect` の大きさとバイト列で見つかった
- `li a0, 0xfffff800` を 2 命令にしていた (32 bit の符号付きに直す前に 1 命令で済むかを判定していた)

### 12.4 まだ無いもの

`.incbin` は試験では置き換えた中身で試した (Linux の `vdso.so` と `initramfs_data.cpio` は nld1 と gen_init_cpio の後にできる)。Linux の inline asm の文字列は ncc1 ができてから通す (M4)。

## 13. M2 の到達点 (2026-10-21)

nld1 ([stage018/nld1.c](../stage018/nld1.c)，約 2,200 行) と素の実行環境 ([tests/stage018/rt](../tests/stage018/rt)) を書いた。nld1 も nas1 と同じく `sh tools/build.sh stage018` が cc15as で組み，試験は kernel29 の上で走らせる。kernel29 で 1 回起動して命令を並べて走らせる部分は [tools/os18.sh](../tools/os18.sh) にまとめた。

### 13.1 nld1 の範囲

| 項目 | 持つもの |
|---|---|
| 入力 | nas1 が出す ELF32 の `ET_REL`。COMMON の記号は擬似的な入力節 `COMMON` にまとめる |
| リンカ台本 | `ENTRY`・`SECTIONS`・`.` と記号への代入 (複合代入を含む)・`PROVIDE` / `PROVIDE_HIDDEN` / `HIDDEN`・出力節の番地・`AT()`・`ALIGN()`・`(NOLOAD)`・入力節の指定 (ワイルドカード，`EXCLUDE_FILE`，`SORT`，`KEEP`)・`/DISCARD/`・`BYTE` / `SHORT` / `LONG` / `QUAD`・`ASSERT`。式は C の演算子と `ALIGN`・`ADDR`・`LOADADDR`・`SIZEOF`・`ALIGNOF`・`DEFINED`・`MAX` / `MIN`・`ABSOLUTE`・`CONSTANT`。`OUTPUT_ARCH` などは読み捨てる |
| 既定の台本 | `-T` が無ければ，0x8000_0000 から `.text`・`.rodata`・`.data`・`.bss` を並べ，後ろに 64 KiB のスタック (`__stack_top`) を置く。既定の台本も同じ解析器で読む |
| orphan | 台本が置かない割り付ける節は，同じ種類 (命令・読出し専用・書込み可・nobits) の最後の出力節の後ろに，同じ名前の出力節として置く。割り付けない節は出力しない |
| 再配置 | 32・64・BRANCH・JAL・CALL / CALL_PLT・PCREL_HI20 / LO12_I / LO12_S・HI20 / LO12_I / LO12_S・ADD / SUB (8・16・32・64)。RELAX は読み捨て，ALIGN は誤りとする |
| 出力 | `ET_EXEC`。大きさのある出力節ごとに PT_LOAD を 1 つ置く (`p_paddr` は LMA)。記号表は入力の局所の記号 (`.L` を除く) と大域の記号を持つ |

書庫・`-r`・`--just-symbols`・`-shared`・`PHDRS`・`--build-id` は M5 で足す。

### 13.2 試験

| 検査 | 結果 |
|---|---|
| 別の .o の記号への再配置 (`tests/stage018/nld1/main.s` + `other.s`) | stone の OS の上で nld1 が結合した実行形式を QEMU で素のまま走らせ，CALL_PLT・PCREL (読み・書き)・HI20 / LO12 (読み・書き)・JAL・BRANCH・32 (関数の表)・ADD32 / SUB32・weak の上書き・weak の未定義 (0)・COMMON のまとめ・台本の記号の 14 項目がすべて正しい |
| リンカ台本 (`lds.s` + `lds.lds`) | 節の番地と大きさ・PT_LOAD の VMA / LMA・記号の値・中身が，台本から手で数えた期待 (`lds.expect`) と verify 層の readelf の読みで一致 ([verify/nld1elf.sh](../verify/nld1elf.sh)) |
| 誤った入力 | 未定義の記号・多重定義・届かない分岐・対応する `%pcrel_hi` の無い `%pcrel_lo`・成り立たない `ASSERT` の 5 種を止める |
| ホスト版との一致 | 2 つの結合で，ホストの gcc で組んだ nld1 と同じ実行形式を出す |

## 14. M3 の到達点 (2026-10-21)

ncc1 ([stage018/ncc1.c](../stage018/ncc1.c)，約 5,800 行) を書いた。4 章の段の構成のとおり，字句解析・構文解析と型付け・定数の畳込み・AST からの直接の生成・参照された static の選別を持つ。`sh tools/build.sh stage018` が cc15as で組み，試験は stone の OS の上で ncc1・nas1・nld1 を 1 回の起動で通し，できた実行形式を QEMU で素のまま走らせる。開発中はホスト版で回す ([tools/ncc18.sh](../tools/ncc18.sh))。

### 14.1 範囲

| 項目 | 持つもの |
|---|---|
| 言語 | C89 のすべて (K&R の関数定義を除く)。`long long`・`_Bool`・`inline`・`//` の注釈・文の後の宣言・`for` の中の宣言・指定付き初期化子 (`.x`・`[i]`・`[a ... b]`・`name:`)・中括弧の省略・無名の構造体と共用体の成員・`case a ... b`・文式 `({ })`・`typeof`・`__extension__`・`_Static_assert`・`__alignof__`・`__label__`・大域の `asm("...")` |
| 属性 | `aligned`・`packed`・`section`・`weak`・`alias`・`used`・`unused`・`mode`・`gnu_inline`・`visibility("hidden")`・`asm("名前")`。他は読み捨てる |
| 組込み関数 | `__builtin_va_start` / `va_arg` / `va_end` / `va_copy`・`expect`・`constant_p`・`offsetof`・`types_compatible_p`・`choose_expr`・`unreachable`・`trap` |
| 生成 | 4.4〜4.6 のとおり。一時値は s1〜s11，フレームは s0 起点，引数は呼出しごとの領域で評価してから a0〜a7 とスタックへ割り付ける。64 bit の除算・剰余だけを実行時支援の呼出しにする |
| 除去 | 4.3 の 2 つ (定数条件の枝・参照されない static) |

浮動小数点・asm 文・レジスタを指定した変数・複合リテラル・計算した goto は持たない (浮動小数点と計算した goto は 9 章のとおり範囲外，残りは M4)。`__builtin_constant_p` は整数定数式だけを 1 とする (4.8)。最適化をするホストの gcc は値の分かっている局所の変数も 1 とするので，試験はこの差に依らない形に書いた。

### 14.2 試験

| 検査 | 結果 |
|---|---|
| C の試験 ([tests/stage018/c](../tests/stage018/c)) 9 本 | 32 bit 以下の演算と変換・64 bit の演算・制御構造と再帰・ポインタと配列と関数へのポインタ・構造体 (配置・ビットフィールド・packed・共用体・無名の成員・値渡しと値返し)・初期化子・可変長引数と多数の引数・到達しない枝の除去・その他 (2 KiB を超えるフレーム・列挙・文式・組込み関数・64 bit の switch・深い式)。stone の OS の上で訳して組んだものが，ホストの gcc (`-funsigned-char`) と同じ出力を出す |
| psABI (`abi.c` + `abi.s`) | psABI の文面から手で書いたアセンブリと C が呼び合う: 64 bit の対・a7 とスタックの分割・可変長の偶数番の対と，対が残らない時のスタックへの移動・2×XLEN 以下と超える構造体の値渡しと値返し (隠れた引数)・狭い整数の拡張。期待は文面から手で書いた |
| 試験の libc ([tests/stage018/rt/libc.c](../tests/stage018/rt/libc.c)) | printf・memcpy・64 bit の除算の支援を ncc1 で訳したもの。前処理器の指令を使わない |
| 誤った入力 | 構文の誤り・宣言の無い名前・構造体からスカラーへの変換・定義の無い static 関数の参照・浮動小数点の 5 種を止める |
| ホスト版との一致 | 試験の 10 本 (libc を含む) で，cc15as で組んだ ncc1 がホストの gcc で組んだ ncc1 と同じ `.s` を出す |

試験が見つけた ncc1 の誤りは 1 件で，構造体の値をスカラーに代入しても止めていなかった。試験の側の誤りが 3 件あった (評価の順の決まっていない式 2 件，最適化に依る `__builtin_constant_p`)。

## 15. M4 の到達点 (2026-10-21)

ncc1 は約 6,900 行になった。rv32-stone の構成の C の単位 374 をホストの cpp で前処理したもの (`sh tools/linux19.sh pre`) を，すべて ncc1 で訳し，nas1 で組み立てられる (`sh tools/linux19.sh ncc`)。404 MB の `.i` は stone の OS のファイルシステム (64 MiB) に入らないので，この測定はホストの cc で組んだ ncc1 と nas1 で行う。stone で組んだ ncc1 がホスト版と同じ `.s` を出すことは試験の C で確かめている (14.2)。

### 15.1 足したもの

| 項目 | 内容 |
|---|---|
| inline asm | 4.7 のとおり。被演算子の名前 (`%[name]`)・`%=`・一致の制約 (`"0"`)・レジスタを指定した変数・64 bit の値 |
| 複合リテラル | 局所は枠に置く。ファイルの有効範囲では static の名前の無い変数にする。静的な初期化子の中の複合リテラル (`(spinlock_t) { ... }`，空の `{ }` を含む) はその初期化子を取り込む |
| 組込み関数 | 4.8 の表の残り (`return_address`・`frame_address`・`object_size`・`prefetch`・ビット演算・`bswap`)。`memcpy` などの名前は暗黙の関数の宣言にする |
| 式 | `x ?: y`・ラベルの番地 `&&label` (`_THIS_IP_`)・添字が実行時の値の `offsetof`・同じ型への構造体のキャスト・記号の番地と空ポインタの比較の畳込み (`f == (T)0 ? f : f`。weak の記号は畳まない)・`(void *)<整数定数式>` を空ポインタ定数として扱う (`__is_constexpr`) |
| 文 | `__label__` の局所のラベル (同じマクロの文式が 1 つの関数に何度も展開される)・ラベルの付いた文式の値 (`({ __label__ h; h: (unsigned long)&&h; })`)・`return` で値が到達しない文式・ラベルの直後の asm 文 |
| 初期化子 | 余る初期化子は読んで捨てる (長さ 0 の配列に `{ 0 }`。GCC は警告に留める)。指示子が同じ場所を 2 度初期化した時は後のものを採る (再配置も外す)，再配置は位置の順に並べる |
| 到達の判定 | `break` と `continue` が到達したかを繰返しと switch ごとに覚え，出口のラベルはそのどちらかか落ちてくる時だけ到達するとする。`do { } while (0)` の先頭へは戻らない。これで定数の switch で選ばれない `case` の中の文 (64 bit の `put_user` など) を出さない |
| 長い関数 | 本体の行の数から長さの上限を見積もり，4 KiB に届きうる関数は文と式の間の条件分岐を反転した分岐と `j` で作り直す。nas1 は緩和をしないので，届かない分岐は ncc1 の側で避ける |
| nas1 | `jump <先>, <レジスタ>` (`auipc` + `jalr`，R_RISCV_CALL)。Linux の `__put_user` の `.fixup` が使う |

### 15.2 試験

| 検査 | 結果 |
|---|---|
| `gnu.c` | 局所のラベル・ラベルの付いた文式の値・`return` を含む文式・260 文の繰返しを持つ長い関数・余る初期化子・番地の比較を畳む静的な初期化子・指示子の上書きと順の違う指示子・静的な初期化子の複合リテラル・同じ型へのキャスト・到達の判定。ホストの gcc と同じ出力 |
| `asm.c` | 64 bit の入力 (下位の語，`%z` と `>> 32`)・`+rm` と `=m` の 64 bit・`jump` 擬似命令・ラベルの直後の asm・定数の switch で除かれる 64 bit の出力。期待は手で書いた (RISC-V の命令はホストで組めない) |
| Linux の C の単位 | 374 単位すべてを訳し，nas1 で組み立てる (前処理した単位がある時だけ。無ければ検査しない旨を出す) |

Linux の単位が見つけた ncc1 の誤りのうち，黙って誤ったものを出しうるものが 3 件あった。

- 指示子で初期化の順が前後すると，静的なデータの再配置が位置の順に並ばず，出力の繰返しが止まらなかった
- 文式の中の `return` で到達しなくなったことが式の外へ漏れ，`?:` の後ろの文が出力から消えていた (`gnu.c` の `early` が見つけた)
- ラベルの付いた最後の文を文式の値として扱わず，値の無い式をキャストしていた

### 15.3 まだ無いもの

- 一時値のレジスタ (s1〜s11) が尽きた時の退避。374 単位では尽きなかった
- レジスタに載る 64 bit の asm の出力，計算した goto
- 11 章の 3 (cc15as で組んだ ncc1 の速さ) の測定。stone の OS のファイルシステムに入る大きさに分けて測る

## 一次資料

2026-10-14 に取得し，[SOURCES.md](SOURCES.md) に固定して記録した (`tools/fetch.sh psabi`・`asmman`・`binutils`)。

- RISC-V ELF psABI Specification (riscv-non-isa/riscv-elf-psabi-doc，批准版 1.0): 呼出し規約 (`riscv-cc.adoc`)，ELF の `e_flags` と再配置の種類と計算式 (`riscv-elf.adoc`)
- RISC-V Assembly Programmer's Manual (riscv-non-isa/riscv-asm-manual): 疑似命令，`%hi` などの修飾子，`.option` (`src/asm-manual.adoc`)
- GNU as と GNU ld の利用者向け文書 (binutils 2.40 の版): 擬似命令とリンカ台本の文法と意味 (`gas/doc/as.texi`・`gas/doc/c-riscv.texi`・`ld/ld.texi`)。文書だけを読み，ソースは参照しない
- 命令の符号化は既に持つ The RISC-V Instruction Set Manual Volume I / II ([SOURCES.md](SOURCES.md)) による
