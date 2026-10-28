# Stage 19: Linux カーネルのビルド (roadmap L2)

## 0. 本書の対象

[roadmap.md](roadmap.md) の L2 (Linux カーネルをコンパイルできること) を進めるための方針と測定結果を記録する。Linux のソースは GCC と同じく stone の処理系を測る入力として使い，stone の標準処理系や実行環境には採用しない ([artifacts.md](artifacts.md) 3 章)。

C99 と GNU 拡張 (roadmap の Stage 18) は，Linux のソースを入力として測りながら実装する。本書はその測定の経緯も持つ。

## 1. 完了条件

1. RV32 の Linux カーネルを，**stone の OS の上で**，stone 製のツールだけで組む。カーネル内のコードはすべて stone の cc が訳す
2. 組んだカーネルを QEMU virt (rv32) で起動し，最小の initramfs の上の init (stone の cc で組んだ静的 ELF) が syscall を確かめて電源を切る

## 2. 決めたこと (2026-10-09)

| 項目 | 決定 |
|---|---|
| 対象 | Linux 5.4.302 (5.4 系の最終版)。`-std=gnu89` のままで，`_Generic` を使う前の版である。`arch/riscv/configs/rv32_defconfig` を持つ |
| 取得 | GitHub の stable ミラー (gregkh/linux) から commit で固定する。kernel.org はこの作業環境から到達できない ([SOURCES.md](SOURCES.md)) |
| 実行の場所 | ビルドはすべて stone の OS (QEMU) の中で行う。ホストは sfs4 のイメージを詰めることと，結果を取り出すことだけをする |
| stone 製の意味 | (a) 我々が書いたツール，または (b) Linux 自身が持つ補助プログラム (fixdep・gen_init_cpio など) を stone の cc で組んだもの。Linux 以外の外部ツール (GNU make・flex・bison・bc) は使わず，(a) で置き換える |
| 開発中の測定 | 壁を早く見つけるため，ホストから単位ごとに我々の pp / cc を並列に呼んでよい (GCC の `tools/gcc17.sh` と同じ方式)。これは完了条件に数えない |
| SBI | 我々の M モードのコードで最小の SBI を書く (OpenSBI を使わない) |

## 3. 要るもの

`Documentation/process/changes.rst` (5.4.302) の最低要件は GNU C 4.6・GNU make 3.81・binutils 2.21・flex 2.5.35・bison 2.0・bc 1.06.95 である。kconfig は `scripts/kconfig/lexer.l` と `parser.y` だけを持ち，生成済みの構文解析器は同梱されていない。

| 区分 | Linux が要るもの | stone の現状 | 方針 |
|---|---|---|---|
| 呼出し規約 | RISC-V psABI (ilp32)。`.S` と C が互いを呼ぶ | 独自の規約 (呼ばれた側が語を数える) | cc に psABI の生成を入れる |
| 番地の求め方 | `-mcmodel=medany` (MMU を有効にする前は PC 相対) | 遠距離呼出しは `lui` の絶対番地 | `auipc` を使う形を入れる |
| C の拡張 | inline asm・`__attribute__`・文式・`typeof`・`__builtin_*`・C99 の初期化子など | C89 (+ 遠距離呼出しの `#pragma`) | Stage 18 として実装する。PR #93 の branch に 5 つ (文の後の宣言・指定付き初期化子・文式・`typeof`・`case a ... b`) の実装がある |
| 命令 | `rv32ima` + Zicsr / Zifencei | RV32IM を生成 | A 拡張と CSR は inline asm とアセンブラで扱う |
| アセンブラ | GNU as の書式 (`.macro`・`.pushsection`・`%pcrel_hi`・`1f` など) | 無い | 新しく書く |
| リンカ | linker script (`vmlinux.lds`) を読み，標準の ELF を出す | ld18 は stone の実行形式を出す | 新しく書くか広げる |
| ar / objcopy / nm | thin archive (`ar cDPrST`)・`-O binary` | ar17 | 広げる |
| make | GNU make 3.81 の Kbuild が使う機能 | mk20 (関数・目標特有の変数・`-C`) | mk を広げる |
| sh | `scripts/*.sh` (`link-vmlinux.sh` など) | sh5 | 足りない構文を足す |
| kconfig | flex / bison で作る `conf` | 無い | Kconfig を読む道具 `kconf` を書く (4 章) |
| bc | `kernel/time/timeconst.bc` (多倍長整数) | 無い | bc の部分集合を書く |
| 補助プログラム | fixdep・gen_init_cpio (・modpost) | - | Linux のソースから stone の cc で組む |
| ファイル系 | ツリーは展開して約 1.2 GB | sfs4 の窓は 512 MiB | 構成が参照するファイルに絞って詰める。足りなければブロックデバイスを足す |

## 4. kconf (Kconfig を読む道具)

`conf` の代わりに Kconfig を読み，`.config` と Kbuild が読む生成物を出す。flex / bison を使わず，手書きの再帰下降で読む。

測った規模 (5.4.302，`Documentation/` を除く): Kconfig は 1518 ファイル，`config` が 17,993 個ある。前処理の関数は `$(shell,…)`・`$(success,…)`・`$(failure,…)`・`$(if-success,…)`・`$(cc-option,…)`・`$(ld-option,…)`・`$(error-if,…)` を使う。

出すもの:

- `.config`
- `include/config/auto.conf`・`include/config/tristate.conf`
- `include/generated/autoconf.h`
- `include/config/` の下の記号ごとのファイル (fixdep が依存として使う)

### 4.1 照合の方法

上流の `conf` を照合の相手にする。`conf` は字句解析器を flex で作るので，flex 2.6.4 を取得して `tmp/` の中で組む (`tools/kconfref.sh flex`。[SOURCES.md](SOURCES.md))。flex は照合の相手を組むためだけに使い，stone のビルドチェーンにもカーネルにも入らない。

`sh tools/kconfref.sh cmp <kconf> <目標>` が突き合わせる。Kbuild が `conf` を呼ぶ形 (環境変数・作業ディレクトリ・引数) のまま kconf を走らせるため，一度上流の `conf` で目標を作った後，`scripts/kconfig/conf` を kconf で置き換え，生成物を消して同じ目標をもう一度作る。比べるのは `.config`・`include/config/auto.conf`・`auto.conf.cmd`・`tristate.conf`・`include/generated/autoconf.h` と，`include/config/` の下の記号ごとのファイルの一覧である。

### 4.2 上流に合わせた点

値は必要になった時に計算して記号ごとに覚えるので，依存が循環すると計算を始めた記号によって答が変わる。上流と同じ答を出すため次を合わせた。

- 計算を始める順。記号のハッシュ表の順 (表の大きさ 9973，FNV-1 32 bit，鎖の先頭へ足す) と menu の順
- 覚えた値を捨てる時点 (`sym_clear_all_valid` と，`auto.conf` を読み直す時)
- menu の木。自動の副 menu は式の簡約の結果で決まり，`.config` の `# end of ...` の位置を左右するので，式の簡約 (同じ項の除去・比較への書換え) も同じ規則で行う

### 4.3 結果 (stage019/kconf1.c，ホストの gcc で組んだもの)

| 目標 | 結果 |
|---|---|
| `allnoconfig` | 一致 (記号ごとのファイル 166 個を含む) |
| `tinyconfig` (`allnoconfig` + `merge_config.sh` + `olddefconfig`) | 一致 |
| `defconfig`・`rv32_defconfig` | 一致 |
| `alldefconfig`・`allyesconfig`・`allmodconfig` | 一致 |
| `savedefconfig` | 一致 |
| `.config` を書き換えた後の `olddefconfig` | 一致 |
| `syncconfig` (ビルドの途中で Kbuild が呼ぶ) | 一致 (記号ごとのファイル 679 個) |

上流と違う点は次のとおりである。対話の入力 (`oldconfig` など) を持たず，`syncconfig` で値の無い新しい記号が見つかれば止まる (先に `olddefconfig` を回す)。依存の循環の検査を持たない。式の木を解放しない (`allyesconfig` で最大 RSS 44 MB)。

### 4.4 stone の OS の上で走らせるために要るもの

kconf1 は C89 で書いてあり，stone の cc で訳す。libc26 には次が無い。

- `getenv`。Kbuild は `srctree`・`SRCARCH`・`CC` などを環境変数で渡すが，stone の OS はプロセスに環境変数を渡していない (`environ[0] == 0`)
- `$(shell,...)` を実行する手段 (上流は `popen`)
- `rename`

## 5. Kbuild が呼ぶもの (実測)

`sh tools/kbuildlog.sh run <出力> <設定の目標>` が，ホストの make で Kbuild を回し，呼ばれた命令を記録する。本物の RISC-V のツールチェインは使わず，引数を記録して空の出力を作るだけの代役 (GCC 4.6.4 と名乗る) を置く。make の SHELL も記録用の台本に替え，レシピと `$(shell ...)` を残す。生成物の中身は空なので，最後のリンクの後 (`mksysmap`) で止まるが，そこまでの命令はすべて記録に出る。

### 5.1 最初の対象の設定

[stage019/rv32-stone.config](../stage019/rv32-stone.config) を `KCONFIG_ALLCONFIG` に渡して `allnoconfig` で作る。`tiny.config` の中身に，RV32I・1 hart・浮動小数点なし・圧縮命令なし・printk・SBI 経由の console・initramfs・ELF の実行を足したものである。`MODULES` と `KALLSYMS` は無い。code model は `medlow` (`lui` による絶対番地) になる。

### 5.2 規模

| | `rv32_defconfig` (参考) | rv32-stone |
|---|---|---|
| C の単位 (`-c`) | 1461 | 445 |
| `.S` の単位 (試しを含む。rv32-stone の単位は 12。[stage018-ncc.md](stage018-ncc.md) 12.1) | 16 | 15 |
| `-E` (リンカ台本など) | 11 | 11 |
| `-S` (asm-offsets などの定数の見出し) | 14 | 14 |
| `ar` | 362 | 205 |

### 5.3 rv32-stone で呼ばれるもの

| 区分 | 中身 |
|---|---|
| Linux 自身の補助プログラム | fixdep，conf (kconf で置き換える)，mk_elfconfig，modpost，gen_init_cpio，gen_crc32table，dtc |
| 外部の道具 | make，sh，echo，rm，mkdir，printf，cat，sed，head，mv，cmp，ln，grep，expr，wc，getconf (`LFS_CFLAGS`)，bc (`kernel/time/timeconst.bc`)，flex と bison (dtc の字句解析器と構文解析器) |
| ツールチェイン | gcc (`-c`・`-S`・`-E`，`--version` などの問合せ，cc-option の試験)，ld，ar，objcopy，objdump，nm |

sh は Kbuild のすべてのレシピで `trap '...; kill -s HUP $$' HUP` の形を使う (割込みで出力を消す)。`scripts/link-vmlinux.sh` は関数と `local` を使う。

**dtc。** riscv は QEMU が渡す device tree を読むために `OF_FLATTREE` を要し，それが `DTC` を選ぶ。そのため `scripts/dtc` が組まれるが，この設定では `.dts` を 1 つも訳さないので dtc は使われない。dtc の字句解析器と構文解析器は flex / bison で生成するので，stone の中で組むには我々の側に生成の手段が要る。

**dtc は stone の中では組まない (2026-10-13 決定)。** この設定で使われないものを組むために字句解析器と構文解析器の生成器を書くのは釣り合わないからである。scripts の段だけ `CONFIG_DTC` を空にして渡し，`scripts/dtc` を飛ばす。これは上流の Kbuild からの逸脱である。`.dts` を訳す設定が要るようになった時点で，dtc の字句解析器と構文解析器を手で書くか，lex / yacc に当たる生成器を書くかを決め直す。

## 6. 我々の cc に要る拡張 (実測)

`sh tools/linux19.sh pre <kbout>` が，rv32-stone の C の単位 374 個を前処理する。前処理はホストの cpp に任せ，ホストの定義を `-undef -nostdinc` で消して，処理系が前もって定義するもの ([stage019/predef-rv32-gcc46.h](../stage019/predef-rv32-gcc46.h)。GCC 4.6.4 / riscv32 / -Os に合わせる) と処理系の `stdarg.h` ([stage019/include](../stage019/include)) を与える。我々の pp はまだ `-I` の探索順を持たないので，ここでは cc に要るものだけを測る。

374 単位すべてが前処理できた。`arch/riscv/mm/init.o` だけは Makefile が `-mcmodel=medany` を指定し，`__riscv_cmodel_medany` が無ければ `#error` で止まる (MMU を有効にする前に走る `setup_vm()` は PC 相対で番地を求める必要がある)。**我々の cc は，少なくともこの単位について `auipc` による PC 相対の番地を生成する必要がある。**

### 6.1 拡張の使われ方

`sh tools/linux19.sh scan <kbout>` が，字面で数えた目安を出す。共通の header (`linux/kernel.h` など) が使うので，ほぼすべての単位にほぼすべての形が現れる。**1 つでも欠ければどの単位も通らない**ので，単位ごとの優先順位は意味を持たない。

| 形 | 単位 (374 中) |
|---|---|
| `__attribute__`・`__extension__`・`inline`・`long long` | 372 |
| inline asm | 369 |
| 文式 `({ ... })` | 368 |
| `__builtin_constant_p` | 365 |
| `__builtin_va_*` | 363 |
| `case a ... b` | 363 |
| `__alignof__` | 362 |
| `typeof` | 350 |
| `__builtin_expect` | 346 |
| 指定付き初期化子 `.x =` | 341 |
| `__builtin_choose_expr` / `__builtin_types_compatible_p`・`__label__` | 334 |
| `__builtin_offsetof`・`__builtin_unreachable` | 327 |
| 長さ 0 の配列 | 323 |
| `_Static_assert` | 228 |
| asm goto・計算した goto | 0 |

### 6.2 中身

- **組込み関数**: `__builtin_constant_p`・`expect`・`types_compatible_p`・`choose_expr`・`offsetof`・`unreachable`・`memcpy`・`object_size`・`va_list` / `va_start` / `va_arg` / `va_end` / `va_copy`・`return_address`・`prefetch`・`extract_return_addr`・`clzll`
- **属性**: `no_instrument_function`・`unused`・`gnu_inline`・`always_inline`・`error`・`format`・`cold`・`aligned`・`section`・`designated_init`・`const`・`malloc`・`noreturn`・`pure`・`assume_aligned`・`nonnull`・`packed`・`noinline`・`externally_visible`・`alias`・`used`・`weak`・`mode` (`__attribute__((` の直後の名前だけを数えた)
- **inline asm の制約**: `r`・`=r`・`=&r`・`+r`・`rJ`・`rK`・`i`・`m`・`=m`・`+A` (不可分操作の番地)・`+rm`。asm 文は字面で 82,735 個ある
- **レジスタを指定した変数**: `register uintptr_t a0 asm ("a0")` (SBI の呼出しで a0〜a7)，`gp`，`sp`

## 7. 段取り (2026-10-22，案)

Stage 18 で，ホストから駆動した Kbuild が我々の道具 (ncc1・nas1・nld1・nar1・nobj1) だけで rv32-stone の `vmlinux` を組み，我々の SBI の上で initramfs の init まで起動した ([stage018-ncc.md](stage018-ncc.md) 16・17 章)。残るのは 1 章の完了条件 1，すなわち**全体を stone の OS の中で回すこと**である。ホストに残っている役目は次のとおり。

| 役目 | 今 (Stage 18) | stone の中で要るもの |
|---|---|---|
| 前処理 | ホストの cpp | pp の新しい世代 (`-I`・`-isystem`・`-include`・`-D`・`-U`・`-Wp,-MMD`) |
| make | GNU make | mk の新しい世代 (Kbuild が使う GNU make の部分集合) |
| sh と小道具 | bash・coreutils | sh の新しい世代と，echo・rm・mkdir・printf・cat・sed・head・mv・ln・grep・expr・wc・getconf・bc |
| 設定 | 上流の conf (flex / bison) | kconf1 (4 章) を stone の cc で組む |
| Linux の補助プログラム | ホストの cc | fixdep・mk_elfconfig・modpost・gen_init_cpio・gen_crc32table を stone の cc (cc15as + libc26) で組む。dtc は組まない (5.3) |
| 駆動 | kb18cc.sh (ホストの sh) | gcc の命令行を受けるドライバ `ncc` (stage018-ncc.md 3 章) |

### 7.1 測ったこと

- **ncc1 の速さ** (stage018-ncc.md 11 章の 3): cc15as で組んだ ncc1 は，stone の OS の上で 1.48 MB の `.i` (2 単位) を起動の分を除いて約 14 秒で訳した (ホスト版と同じ `.s`)。374 単位 388 MB で 1 時間ほどになる。**ncc1 の自己ホストは要らない**
- **木の大きさ**: Kbuild の依存の記録 (`.*.cmd`) が引くソースと header は 1,556 ファイル 17 MB，Makefile・Kbuild・Kconfig は木全体で 7.6 MB である。木全体 (1.2 GB) を詰める必要はない
- **sh が走らせる命令** (kblog の shell.log，レシピと `$(shell ...)`): `trap` 33,627・`kill` 16,775 (どのレシピも `trap ...HUP` の形)・`set` 3,433・`echo` 809・`rm` 641・`mkdir` 290・`printf` 222・`make` 214・`cat` 155・`sed` 11・`sh` 10・`head` 9・`mv` 7・`getconf` 6・`ln`・`grep`・`expr`・`wc`・`bc` は数回

### 7.2 進め方 (2026-10-22 決定)

**最初から stone の OS の上で回す。** 我々の道具をホストの cc で組んで測る段は設けない。stone の libc 固有の関数の代わりを書く手間がかからない代わりに，1 回の試行は遅くなる。試行を速くするため，Kbuild を最初から全体で回すのではなく，道具ごとに Kbuild が使う形を取り出した小さな入力で確かめてから合わせる。

| 部 | 内容 | 終わりの判定 |
|---|---|---|
| S1 | pp の新しい世代 (`-I`・`-isystem`・`-include`・`-D`・`-U`・`-Wp,-MMD`) | stone の上で，374 の C の単位・`.S`・リンカ台本・asm-offsets の `.i` がホストの cpp と同じになる。依存の記録を fixdep が読める |
| S2 | ドライバ ncc (C で書く。stage018-ncc.md 3 章) | stone の上で，Kbuild が出す gcc の命令行のまま単位を組める |
| S3 | mk の新しい世代 | stone の上で Kbuild の Makefile を読んで，回すべき命令を回す |
| S4 | sh の新しい世代と小道具，bc | Kbuild のレシピと scripts/*.sh が走る |
| S5 | kconf1・Linux の補助プログラムを stone の cc で組む。OS と libc に環境変数・`popen`・`rename` など足りないもの | 設定の目標と `vmlinux` の目標が stone の中で通る |
| S6 | 全体を stone の OS の中で回す | 1 章の完了条件 1・2 |

mk・sh・sed などの道具は，これまでの世代と同じく，今の世代 (mk20・sh5・sed3 など) を全文複製した新しい世代に足していく (2026-10-22 決定)。

### 7.3 Stage 18 からの引継ぎ

stage018-ncc.md 4.3 の変更 (除去されずに残った `error` 属性の関数の呼出しを `ebreak` にし警告を出す。rv32-stone では 2 箇所) は承認された (2026-10-22)。

## 8. S1: 前処理器 npp1 (2026-10-22)

`stage019/npp1.c`。C89 で書き，stage018 の道具と同じく cc15as と libc26 で組む (`tools/build/stage019.sh`)。pp21 (stage017) は sc の方言で書かれ，バンドルを入力に取り `-I` しか持たないので，広げずに新しく書いた。

### 8.1 受けるもの

GCC の cc1 の前処理の部分集合で，Linux 5.4 の rv32-stone が使うもの。

- 選択肢: `-I`・`-isystem`・`-iquote`・`-include`・`-D`・`-U`・`-P`・`-x c|assembler-with-cpp`・`-MD <file>`・`-MMD <file>`・`-MF`・`-MT`・`-o`。`-nostdinc`・`-undef`・`-E` は読み捨てる (前もって定義するものは持たず，ドライバが `-include stage019/predef-rv32-gcc46.h` で与える)。知らない選択肢は誤り。入力 `-` は標準入力
- 展開: 字句の列と hide set による展開 (Prosser の方式)。`#`・`##`・配置用の字句，GNU の `args...` と `, ## __VA_ARGS__`，`__LINE__`・`__FILE__`・`__COUNTER__`，`_Pragma` (被演算子もマクロを展開する。Linux の `__diag` が使う)
- 指令: `#if`・`#elif` (64 bit の符号付き・符号無しの算術)・`#ifdef`・`#ifndef`・`#else`・`#endif`・`#define`・`#undef`・`#include`・`#include_next`・`#error`・`#warning`・`#pragma` (`once` を含む)・`#line`・`#ident`。include guard を見分けて 2 度目を開かない
- assembler-with-cpp: 行の頭の `#` で始まる知らない行 (注釈) を残す。閉じない `'` を通す。1 つの字句にならない `##` は誤りにせず 2 つの字句のまま並べる

### 8.2 GCC と合わせたこと

出力の空白と依存の記録は，意味を持たない所も GCC 13 (ホストの cpp) と同じにした。Kbuild はどちらも読む (`gcc-version.sh` は出力の最後の行を読み，fixdep は依存の記録を読む)。

- `-P` では空行を出さない
- 字句の間の空白は，元に空白があった所と，展開の境 (置換の前後・実引数の前後・空に展開した後) で 2 つの字句がつながって読める所 (`cpp_avoid_paste` の表) にだけ置く。ファイルやマクロの本体で隣り合っていた字句はそのまま並べる (`.data..percpu` を壊さない)
- 依存の記録: 頭の `./` を除く。`-include` は読む順に記録する。同じファイルを `(書かれた名前, 見つけた探索の階層)` が違う形で 2 度読めば 2 度記録する (`-include` の絶対経路と `#include <...>`，`<linux/taskstats.h>` と `<uapi/linux/taskstats.h>`)。libcpp がファイルをその組で覚えるからである
- `-include` は作業の場所から先に探す
- `__LINE__` は字句の行を返す。マクロの本体から来た字句はマクロの名前の行，実引数の中の字句は自身の行になる (複数行にわたる呼出しで閉じ括弧の行にならない)
- `, ## __VA_ARGS__` は可変長の実引数が書かれていない時 (`F(a)`) だけ読点を消し，書かれて空の時 (`F(a,)`) は残す
- `#line` は展開してから読み，次の行の番号と `__FILE__` の名前を替える
- `#if` の `&&`・`||`・`?:` で結果に効かない被演算子は 0 で割っても誤りにしない

### 8.1.1 記憶域

libc26 の `malloc` は K&R の方式で，`free` がアドレス順のフリーリストを線形に辿る。npp1 は展開の度に小さな塊を確保・解放するので，そのままでは断片が増えて stone の上で処理が二乗になる (ホストの libc では表れない)。npp1 は塊を 2 の冪の級に切り上げ，解放した塊を級ごとの列に持って次の確保に使い，libc へは返さない (`xmalloc`・`xrealloc`・`xfree`)。

### 8.3 確かめたこと

| 何を | どう | 結果 |
|---|---|---|
| C の単位 374 | `tools/linux19.sh npp`: npp1 の `.i` を ncc1 に通した `.s` を，ホストの cpp の `.i` から作った `.s` と比べ，`.i` も空白を除いてホストの cpp と比べる | 374 すべて一致 |
| `.S` の単位 12 | `tools/linux19.sh npp <kbout> asm`: nas1 で組み立てた `.o` と，行の中の空白を詰めた `.s` を比べる | 9 一致，1 は `.incbin` の材料が記録の木に無いので `.s` だけ比べて一致，2 は標準入力の単位 (前処理しない) |
| リンカ台本 2・asm-offsets | `tools/linux19.sh npp-x` | すべて一致 |
| Kbuild 全体 | `tools/kb18.sh run` を KB18_NPP 付きと無しで回し，`tools/kb18.sh cmp` で比べる | 1,260 個 (`.o`・`.a`・`.so`・`vmlinux`・`Image`・`.config`・依存の記録 `.cmd` 637) がすべて一致。起動 (tools/boot18.sh) も通る |
| stone の上 | `tools/npp19.sh stone`: stone の npp1 に Kbuild と同じ命令行を走らせ，ホストの npp1 と `.i`・`.d` をバイト単位で比べる | `mm/highmem.o`・`kernel/exit.o`・`arch/riscv/kernel/head.o`・`vmlinux.lds`・`asm-offsets.s` がすべて一致 (tests/stage019)。1 単位は起動と詰める時間を含めて約 2.5 分 |

比べる相手のホストの cpp には `-U__has_attribute` を渡す (`linux19.sh` の `PP=ref`)。GCC 13 の cpp は `__has_attribute` を組み込みで持ち，GCC 4.6 は持たない。Linux は無い時に `__GCC4_has_attribute_*` の表を引くので，そのままでは `__designated_init__` などの属性が GCC 4.6 と違う形で付く。

### 8.4 S1 で直した Stage 18 の道具

- `tools/kb18cc.sh`: 依存を求められていない時にも `-MT` を付けていた。gcc は `-MD` / `-MMD` の無い `-MT` を誤りとするので，Kconfig と Kbuild の試し (`cc-option` など，`-o` を伴うもの) がすべて落ちていた。Stage 18 の rv32-stone はそのため `-march=rv32ima` など少ない選択肢で組まれていた。直した後は試しが通り，`CONFIG_CC_HAS_ASM_INLINE` などが入る。この設定でも起動は通る
- `tools/kb18cc.sh`: 中間の `.i`・`.s` を出力の隣に置いていたので，`-o /dev/null` の試しを並べて走らせると取り合った。一時ファイルに移した
- `tools/kb18.sh`: `KBUILD_BUILD_TIMESTAMP`・`KBUILD_BUILD_USER`・`KBUILD_BUILD_HOST` を固定し，同じ入力から同じ `vmlinux` を作る

## 9. S2: ドライバ ncc と kernel30 (2026-10-26)

### 9.1 ncc

`stage019/ncc.c`。Kbuild が CROSS_COMPILE の gcc として呼ぶドライバで，Stage 18 の `tools/kb18cc.sh` (ホストの sh で書いた同じ役目のもの) を C で書き直したものである。命令行を読み，npp1・ncc1・nas1・nld1 を別のプログラムとして順に起動する (2026-10-26 決定。道具を 1 つに結合すると起動は速いが，npp1 と ncc1 を結合できる形に直す手間がかかる)。

| 形 | 動き |
|---|---|
| `-E` | npp1 だけ。`-o` が無ければ標準出力へ |
| `-S` / `-c` の `.c` | npp1 → ncc1 (→ nas1) |
| `-c` の `.S` | npp1 (assembler-with-cpp) → nas1 |
| `-c -x assembler` | nas1 (前処理しない) |
| それ以外 | nld1 で結合する (`-Wl,` を分け，`-shared`・`-s`・`-r`・`-T` を渡す)。入力が `-` なら読み捨てて通す (Kconfig の試し) |
| `--version`・`-dumpversion`・`-print-file-name=…`・`-v` | GCC 4.6.4 として答える (kb18cc.sh と同じ文字列) |

- 前処理の選択肢: `-Wp,-MMD,<f>` は `-MMD <f>` にする。依存を求められた時だけ `-MT <出力>` を足す (`-o` が無ければ GCC と同じく入力の名前から `.o` を作る)。処理系の前定義は `-include` で必ず渡す。`-mcmodel=medany` は npp1 (`__riscv_cmodel_*`) と ncc1 に伝える。知らない `-W*`・`-f*`・`-m*` などは読み捨てる
- 置き場: 道具・前定義・処理系の include の階層は，環境変数 `NCC_BIN`・`NCC_PREDEF`・`NCC_INCLUDE`，無ければ `/lib/ncc` の下を使う。stone の OS は今は環境変数を渡さない (libc26 の `getenv` は常に 0) ので，stone では `/lib/ncc` に置く
- 中間ファイル: 出力の名前に `.ncc.i` / `.ncc.s` を足した所に置き，終わりに消す。出力が `/dev/` の下 (Kconfig の試しの `-o /dev/null`) か無い時は，作業の場所に `ncc-tmp-<番号>` で置く。ホストでは番号はプロセスの番号で，並べて走る試しでも取り合わない。stone の走行は逐次で `getpid` は常に 1 を返すが，取り合う相手が居ない
- 子の起動: stone では libc26 の `spawn`，ホスト (`-DNCC_HOST`) では `fork`・`execv`・`waitpid`。同じソースを両方で組む

### 9.2 kernel30

`stage019/kernel30.c`。kernel29 の全文複製に spawn の引数の上限の拡大だけを入れた。Kbuild の gcc の命令行は最大 87 語・2.2 KB ある (rv32-stone の全命令の `.cmd` から数えた) が，kernel29 は 64 語・4096 バイトまでしか写さず，65 語目で E2BIG を返す。

| | kernel29 | kernel30 |
|---|---|---|
| 引数の数 (`NARGV`) | 64 | 1024 |
| 引数の文字列の全長 (`NARGS`) | 4096 バイト | 32768 バイト |

引数はユーザスタックの上端の下に写すので，フレームスタック 16 MiB のうち約 36 KiB を占める。syscall と配置は変えていない。7.2 では OS の変更を S5 に置いたが，S2 の終わりの判定 (stone の上で Kbuild の gcc の命令行のまま組める) に要るので S2 で入れた。`tools/os18.sh` は `OS18_KERNEL` でカーネルを選べる (既定は kernel29 のまま)。

### 9.3 確かめたこと

| 何を | どう | 結果 |
|---|---|---|
| Kbuild 全体 | `tools/kb18.sh run` を `KB18_NCC` (ホスト版の ncc) で回し，ホストの cpp で回した基準と `tools/kb18.sh cmp` で比べる | 1,260 個 (`Image` と依存の記録 `.cmd` を含む) がすべて一致。中間ファイルは残らない |
| stone の上 | `tools/ncc19.sh stone`: Kbuild の記録の gcc の命令行を，頭の gcc を `/lib/ncc/ncc` に替えただけでそのまま kernel30 の上で走らせ，ホスト版の ncc と出力・依存の記録をバイト単位で比べる | `mm/highmem.o` (`-c` の `.c`)・`arch/riscv/kernel/head.o` (`.S`)・`asm-offsets.s` (`-S`)・`vdso.so.dbg` (`-shared -s` の結合) がすべて一致。4 つで約 4 分 |
| 引数の上限 | 101 語の引数で ncc を起動する (tests/stage019) | kernel30 は通り，kernel29 は通らない |

`-Wl,-T,<台本>` のように `-Wl,` の中に書かれた名前も，stone の根へ写すファイルとして拾う必要があった (ncc19.sh)。

## 10. S3: mk の新しい世代 mk21 (2026-10-28，設計)

### 10.1 測ったこと

rv32-stone の Kbuild が読む Makefile は 231 個・384 KB・11,007 行 (トップの Makefile，scripts/Kbuild.include・Makefile.*，arch/riscv/Makefile，降りる階層の Makefile と Kbuild)。サブ make は 212 回起動される。2 度目からは各階層の `.<名前>.cmd` (計 16 MB) も -include で読む。

| 種類 | 使われ方 (出現の数) |
|---|---|
| 関数 | call 244・if 124・filter 52・patsubst 51・addprefix 50・shell 48・wildcard 43・filter-out 40・sort 38・subst 37・foreach 33・strip 20・notdir 19・dir 17・findstring 16・firstword 10・basename 10・error 8・origin 7・eval 7・abspath 6・warning 5・realpath 5・addsuffix 4・words 3・lastword 3 |
| 指令 | ifeq 110・ifdef 97・ifneq 65・export 59・else 49・include 42・define 10・ifndef 9・-include 9・unexport 3 |
| 特別な目標 | .PHONY・.DELETE_ON_ERROR・.SECONDARY・.SECONDEXPANSION |
| 自動変数 | `$@` 235・`$<` 68・`$*` 15・`$(@D)` 14・`$^` 7・`$(@F)` 7・`$?` 2 |
| 規則の形 | 静的型規則 12・順序だけの依存 (`|`) 1・置換参照 (`$(m:.o=-y)`) 9 |
| make の変数 | MAKECMDGOALS 26・MAKEFLAGS 14・CURDIR 4・MAKEFILE_LIST 3・MAKE_VERSION 2 |

mk20 (1,743 行) は固定の大きさの表 (規則 256・変数 512・展開 16 KiB) で，`define`・`eval`・`export`・静的型規則・複数の目標・二次展開・読み込んだ Makefile の作り直しを持たない。

### 10.2 決めたこと (2026-10-28)

- mk21 は mk20 を全文複製したうえで作り直す。表は動的に確保し，規則の扱い (複数の目標・静的型規則・順序だけの依存・目標特有と型特有の変数・`.SECONDEXPANSION`) を作り直す。GNU make のソースは参照せず，GNU make の文書が定める振舞いを実装する
- 環境変数は S3 で OS に入れる (kernel31・libc27)。mk21 は `export` した変数と `MAKEFLAGS` を環境として子へ渡す。7.2 では S5 に置いていた
- 確かめは ncc と同じく，mk21 をホスト向けにも組み (`MK_HOST`)，ホストの sh で命令を走らせて Kbuild 全体を GNU make と比べる。stone の上では，S4 の sh を待たずに確かめられる範囲 (小さな記述と sh2 で走る命令) で確かめる

### 10.3 mk21 が持つもの

- 読み: 行の継続，注釈，`=`・`:=`・`::=`・`+=`・`?=`，`override`・`export`・`unexport`，`define`/`endef`，`ifeq`/`ifneq`/`ifdef`/`ifndef`/`else` (`else ifeq` の形を含む)，`include`/`-include`/`sinclude`
- 展開: `$(v)`・`${v}`・`$v`，名前の展開，置換参照，関数 (10.1 の全部と `info`・`word`・`wordlist`・`suffix`・`and`・`value`・`flavor`)。`eval` は展開した文字列を記述として読む
- 規則: 複数の目標，型規則 (複数の目標の型規則は 1 回で全部を作る)，静的型規則，順序だけの依存，`::`，目標特有と型特有の変数，`.PHONY`・`.DELETE_ON_ERROR`・`.SECONDARY`・`.SECONDEXPANSION`
- 実行: 命令は行ごとに展開し，行ごとに `$(SHELL) -c` で走らせる (stone では S4 の sh。今は sh2 に一時ファイルで渡す)。`@`・`-`・`+` の頭。`$(MAKE)` を含む行は `-n` でも走らせる。古さは時刻で判定する
- 再帰: `$(MAKE)` は自身。命令行の変数の代入と選択肢は `MAKEFLAGS` で子へ渡す。`MAKELEVEL`・`CURDIR`・`MAKECMDGOALS`・`MAKEFILE_LIST`。`MAKE_VERSION` は比べる相手の GNU make と同じ 4.4.1 と答える
- 読み込んだ Makefile に規則があれば作り直し，作り直したら最初から読み直す (Kbuild の `include/config/auto.conf`)
- 選択肢: `-f`・`-C`・`-n`・`-s`・`-k`・`-r`・`-R`・`-B`・`-j` (受けて逐次に走らせる)・`--no-print-directory`・`V=` などの変数
