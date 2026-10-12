# Stage 19: Linux カーネルのビルド (roadmap L2)

## 0. 本書の対象

[roadmap.md](roadmap.md) の L2 (Linux カーネルをコンパイルできること) を進めるための方針と測定結果を記録する。Linux のソースは GCC と同じく stone の処理系を測る入力として使い，stone の標準処理系や実行環境には採用しない ([artifacts.md](artifacts.md) 3 章)。

C99 と GNU 拡張 (roadmap の Stage 18) は，Linux のソースを入力として測りながら実装する。本書はその測定の経緯も持つ。

## 1. 完了条件

1. RV32 の Linux カーネルを，**stone の OS の上で**，stone 製のツールだけで組む。カーネル内のコードはすべて我々の cc が訳す
2. 組んだカーネルを QEMU virt (rv32) で起動し，最小の initramfs の上の init (我々の cc で組んだ静的 ELF) が syscall を確かめて電源を切る

## 2. 決めたこと (2026-10-09)

| 項目 | 決定 |
|---|---|
| 対象 | Linux 5.4.302 (5.4 系の最終版)。`-std=gnu89` のままで，`_Generic` を使う前の版である。`arch/riscv/configs/rv32_defconfig` を持つ |
| 取得 | GitHub の stable ミラー (gregkh/linux) から commit で固定する。kernel.org はこの作業環境から到達できない ([SOURCES.md](SOURCES.md)) |
| 実行の場所 | ビルドはすべて stone の OS (QEMU) の中で行う。ホストは sfs4 のイメージを詰めることと，結果を取り出すことだけをする |
| stone 製の意味 | (a) 我々が書いたツール，または (b) Linux 自身が持つ補助プログラム (fixdep・gen_init_cpio など) を我々の cc で組んだもの。Linux 以外の外部ツール (GNU make・flex・bison・bc) は使わず，(a) で置き換える |
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
| 補助プログラム | fixdep・gen_init_cpio (・modpost) | - | Linux のソースから我々の cc で組む |
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

kconf1 は C89 で書いてあり，我々の cc で訳す。libc26 には次が無い。

- `getenv`。Kbuild は `srctree`・`SRCARCH`・`CC` などを環境変数で渡すが，stone の OS はプロセスに環境変数を渡していない (`environ[0] == 0`)
- `$(shell,...)` を実行する手段 (上流は `popen`)
- `rename`
