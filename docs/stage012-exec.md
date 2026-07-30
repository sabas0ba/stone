# Stage 12: 実行環境の移行 (Linux ユーザランドと syscall 層) 設計文書

## 1. 目的と位置づけ

### 1.1 目的

Stage 11 までの処理系はベアメタル QEMU virt 上で動き，外界との接点は
UART (getc / putc) と test finisher (exit) しかない。ファイルが無いので
「複数の入力を開く」ことすらできず，リンカへはオブジェクトを連結して
標準入力から与えている ([stage008-elf-ld.md](stage008-elf-ld.md) 2.3)。

Stage 12 の目的は **実行環境を Linux ユーザランドへ移し，syscall を
獲得する** ことである。これで次が可能になる。

- ファイル I/O (`open` / `read` / `write` / `close`)。`stdio.h` の土台
- 記憶域の動的な要求 (`brk`)。malloc の固定領域 (1 MiB) の上限が外れる
- コマンドライン引数 (`argc` / `argv`)。cc や ld を「コマンド」として
  起動できるようになり，Stage 13 (自立したビルド道具) の前提になる

[roadmap.md](roadmap.md) Phase B の 2 段目である。

### 1.2 非目標

- 動的リンク。静的リンクのみ (Stage 8 から変わらず)
- スレッド・シグナル・プロセス生成 (`fork` / `exec`)。プロセス生成は
  Stage 13 (シェル相当) で必要になった時点で扱う
- 浮動小数点 (従来どおり)
- ベアメタル環境の廃止。**既存 Stage の生成物とテストは一切変えない**
  (5.1)

## 2. 決定: 案 X (Linux ユーザランド)

[roadmap.md](roadmap.md) 2.2 の 2 案を比較し，**案 X を採る**。

| 案 | 内容 | 判断 |
|---|---|---|
| X | Linux ユーザランドを実行環境とする (syscall を叩く側になる) | **採用** |
| Y | 自前の OS を作る | 見送り |

理由:

1. **長期目標との整合。** L1 (GCC のビルド) には数千ファイルの読み書きと
   プロセス起動が要り，L2 は「Linux をビルドする」ことそのものである。
   Linux の syscall ABI を話せるようになることは L2 への直接の前進であり，
   案 Y のファイルシステム・プロセス管理の自作は L1 を数 Stage ぶん遠ざける
2. **純度ポリシーとの両立。** [plan.md](plan.md) 2.1 が禁じるのは
   「ビルド経路に他者のツールを差し込むこと」である。OS (カーネル) は
   QEMU と同じく実行環境であり，ビルド経路の純度を損なわない。
   生成物はすべて自作の cc / ld が作る
3. **可逆性。** syscall 層は libc の下端 1 層に閉じる (4.4)。案 Y へ
   転じたくなった場合も，自作カーネルに同じ syscall ABI を持たせれば
   上のすべてがそのまま動く

### 2.1 実行系は QEMU ユーザモード (qemu-riscv32)

Linux ユーザランドの実行には 2 つの形がある。

| 形 | 内容 |
|---|---|
| ユーザモード | `qemu-riscv32` がゲストの syscall をホストへ変換する。カーネル不要 |
| システムモード | RV32 の Linux カーネルを起動し，その上で実行する |

**ユーザモードを採る**。カーネルイメージもファイルシステムイメージも
不要で，ゲストのプログラムを「ホスト上のコマンド」として直接起動でき，
stdin / stdout / 終了コードがそのまま繋がる (テストが従来の形のまま書ける)。
syscall ABI は実カーネルと同一なので，将来 (L2 で自作ビルドの Linux が
起動した時点で) システムモードへ移しても生成物は変わらない。

環境への追加は Debian の `qemu-user` パッケージ 1 つである
(`env/Containerfile` へ追加し，`packages.lock` を再生成する。
実行環境の追加であり，as / ld を使わない純度ポリシーとは無関係)。

## 3. 何が変わるか (全体像)

```
ベアメタル (従来):
  ld -> フラットバイナリ (0x8000_0000 固定)
  前置部: レジスタ初期化, main 呼出し, getc/putc/exit (UART / test finisher)

Linux ユーザランド (Stage 12):
  ld12 -> ELF 実行形式 (ET_EXEC, プログラムヘッダ付き)
  前置部: 2 スタックの確保と初期化, argc/argv の受け渡し, main 呼出し,
          syscall スタブ (read/write/openat/close/brk/exit),
          getc/putc/exit の互換シンボル
```

コンパイラ (cc) と言語・ABI は **変えない**。変わるのはリンカの出力形式と
前置部 (crt0 相当)，そして libc の環境依存部だけである。

### 3.1 ELF 実行形式

ld の新世代 (ld12。stage012/ に置き，前段の成果物 stage008 の ld で
ビルドする) に ELF 実行形式の出力を足す。

- `ET_EXEC`，機械は RV32。プログラムヘッダは 2 本
  (`PT_LOAD` text+rodata 相当 / `PT_LOAD` bss。`.bss` は `p_filesz = 0,
  p_memsz = 大きさ` で表し，カーネルが 0 埋めする。ベアメタルの
  「QEMU の初期 RAM が 0」という前提 ([stage008-elf-ld.md] 5 章) が，
  ここで仕様に裏づけられた前提に変わる)
- ロードアドレスは 0x0001_0000 (RV32 Linux の慣例的な下限以上)。
  ベアメタルの 0x8000_0000 とはアドレスが変わるだけで，再配置の仕組みは
  従来のまま使える
- エントリポイントは前置部の先頭
- セクションヘッダは付けない (実行には不要。readelf での検査は
  プログラムヘッダで足りる)

従来のフラット出力は残す。**入力の先頭 1 バイトで出力形式を選ぶ**
(`'F'` = フラット (従来)，`'E'` = ELF 実行形式。オブジェクト連結の前に
置く)。ベアメタルの ld.bin (stage008) は変更しないので，既存 Stage の
SHA-256 はすべて維持される。

### 3.2 プロセス開始と 2 本のスタック

本処理系の ABI はフレームスタック (x2) とデータスタック (x9) の 2 本を
固定アドレスに置いてきた ([stage005-sc.md](stage005-sc.md))。Linux では
アドレス空間はカーネルが決め，x2 (sp) には argc / argv / envp の積まれた
スタックが与えられる。前置部で次のように整える。

- **フレームスタック**: カーネルが与えた sp をそのまま使う (x2 は
  もともとフレームスタックレジスタであり，コード生成は変わらない)
- **データスタック**: 前置部が `.bss` に確保した固定領域 (1 MiB) の
  末尾を x9 に置く。ベアメタルの固定アドレスが「リンカが割り付けた領域」に
  変わるだけである
- **argc / argv**: sp 上の配置 (argc, argv[0..], NULL, envp...) から
  argc と argv を取り出し，本処理系の呼出し規約 (データスタック渡し) で
  `main(argc, argv)` へ渡す。引数を使わない既存の `main()` もそのまま動く
  (取り出す個数は仮引数の個数で決まる。[stage010-c89.md] 16 章)
- `main` の返却値を `exit` syscall へ渡す

### 3.3 syscall の獲得

syscall は `a7` に番号，`a0`〜`a5` に引数を置いて `ecall` する
(RV32 Linux ABI)。`ecall` を出す手段は 2 つ考えられる。

| 案 | 内容 | 判断 |
|---|---|---|
| cc に組込み関数を足す | `__syscall(n, ...)` が ecall を発行 | 見送り |
| **前置部にスタブを置く** | ld12 が read/write/... を外部シンボルとして提供 | **採用** |

前置部方式を採るのは，ベアメタルの getc / putc / exit と同じ形だから
である (「実行環境が提供する関数はリンカの前置部が実体を持つ」
[stage008-elf-ld.md] 2.2)。cc は一切変わらず，環境の違いは ld の世代の
違いに閉じる。

第 1 弾のスタブ (RV32 Linux の syscall 番号):

| シンボル | 番号 | 備考 |
|---|---|---|
| `read` | 63 | |
| `write` | 64 | |
| `openat` | 56 | RV32 に `open` (旧形式) は無い。libc の `open` は `openat(AT_FDCWD, ...)` で実装する |
| `close` | 57 | |
| `brk` | 214 | 戻り値は「新しいブレーク」(Linux 生の仕様)。libc 側で包む |
| `exit` | 93 | |

スタブは本処理系の呼出し規約 (データスタック渡し) で呼ばれ，引数を
`a0`〜へ移して `ecall` し，戻り値 (`a0`。負値は -errno) をデータスタックへ
積んで返る。**getc / putc は read / write の 1 バイト版として同じ前置部で
提供する**。これで Stage 11 までの libc とテスト資産が無改変で Linux 上でも
動く。

### 3.4 errno

syscall の失敗は戻り値 -1〜-4095 (= -errno) で返る。libc の環境部が
大域変数 `errno` へ写し，関数は C の慣例どおりの値 (-1 や NULL) を返す。
`errno.h` には当面必要な値 (ENOENT, EBADF, ENOMEM, EINVAL など) だけを
定義する。Stage 11 で保留した `strtol` の ERANGE もここで導入できる。

## 4. libc の再編: 純粋部と環境部

### 4.1 分け方

Stage 11 の libc は「OS を要さない範囲」だった。Stage 12 で環境依存の
コードが初めて入るので，置き場を分ける。

```
lib/            純粋部 (環境に依存しない。従来どおり)
  string.c ctype.c stdlib.c
lib/linux/      環境部 (Linux ユーザランド専用)
  sys.c         open / errno / brk を包む sbrk 相当
  morecore.c    malloc の供給源 (brk 版)
  stdio.c       FILE と stdio (第 3 部)
```

### 4.2 malloc の下回りの差し替え

Stage 11 第 2 部の設計どおり，差し替えるのは `morecore` 1 関数である
([stage011-libc.md](stage011-libc.md) 7.1)。固定領域版 morecore を
`stdlib.c` から環境部へ切り出し，

- ベアメタル: 固定領域版 morecore (従来の動作。既存テストのため残す)
- Linux: brk 版 morecore (上限はアドレス空間まで)

の 2 オブジェクトを用意する。利用者はどちらか一方を stdlib.o と並べて
リンクする。「必要なオブジェクトだけ並べる」というリンクの単位の設計
([stage011-libc.md] 2.2) がそのまま環境の選択にもなる。

### 4.3 stdio.h (第 3 部)

最小の FILE を設計する。

- `FILE` は fd と 1 バイトの押し戻し (ungetc 用) を持つ構造体。
  **バッファリングはしない** (正しさが先。速度は測ってから)
- `stdin` / `stdout` / `stderr` は fd 0 / 1 / 2 に固定で結ばれた実体
- 第 1 弾: `fopen` `fclose` `fgetc` `fputc` `fread` `fwrite` `fgets`
  `fputs` `feof` `ferror` `fflush` (無バッファなので何もしない)
- `printf` / `fprintf` は `%d` `%u` `%x` `%c` `%s` `%%` と最小の幅指定
  だけを実装する (可変長引数は Stage 10 第 3 部の 2 で実装済み)
- `fseek` / `ftell` は `lseek` (62) を足した時点で入れる (第 3 部の裁量)

### 4.4 依存の向き

```
利用者プログラム
  ├── string.o / ctype.o / stdlib.o   (純粋部。環境を知らない)
  └── lib/linux/*.o                   (環境部。syscall スタブを呼ぶ)
        └── 前置部 (ld12)             (syscall スタブの実体)
```

環境を知るのは環境部と前置部だけである。案 Y (自作 OS) へ転じる場合も，
差し替えはこの 2 層で済む。

## 5. 既存資産との関係

### 5.1 ベアメタルは凍結して残す

Stage 2〜11 の生成物・テスト・SHA-256 は一切変えない。ブートストラップ鎖
(hex0 → … → cc10l) はベアメタル上で動き続け，Stage 12 の成果物 (ld12 と
環境部) はその鎖の先に足される。cc 自身を Linux 上で動かす (自身を ELF
実行形式でリンクし直す) のは Stage 13 の課題である。

### 5.2 テスト実行系

`tools/run-qemu-user.sh` を足す (`qemu-riscv32 <prog> [args...]`)。
stdin / stdout / 終了コードは直結なので，従来のテストの形 (`expected/` との
照合) がそのまま使える。test finisher は使わない (終了コードは `exit`
syscall で返る)。

`env/Containerfile` に `qemu-user` を追加し，`packages.lock` を再生成する
(`env.sh lock`)。Containerfile の変更で CI のコンテナ像キャッシュも
自然に作り直される。

## 6. 部割りと検証計画

| 部 | 内容 | 検証 |
|---|---|---|
| 第 1 部 | ld12 (ELF 実行形式 + Linux 前置部: 2 スタック・argc/argv・exit/write/read スタブ・getc/putc 互換)，run-qemu-user.sh，環境の更新 | hello (putc) が qemu-riscv32 で動き終了コードが返る。readelf でプログラムヘッダを検査。argc/argv を写すプログラム。Stage 11 の libc テスト (str/cty/mal/srt/num/word) を Linux 上でも実行して同値 |
| 第 2 部 | syscall 層 (openat/close/brk)，errno，brk 版 morecore | ファイルの読み書きと内容照合。存在しないファイルで errno = ENOENT。malloc が 1 MiB を超えて確保できる |
| 第 3 部 | stdio (FILE / fopen 系 / printf 最小) | 値の照合 (書式の境界: 0, 負数, INT_MIN, %x, 幅)。ファイル経由の往復。cat 相当・wc 相当の自己適用 |

各部とも従来どおり: 設計は本書へ追記し，生成物の SHA-256 を記録し，
tests/stage012 で照合する。

## 7. Stage 13 への接続

第 3 部まで終えると「ファイルを開き，読み書きし，引数を受け取り，
終了コードを返す」コマンドが書けるようになる。Stage 13 はこの上に
ar / make 相当 / シェル相当を作り，cc / ld 自身を Linux 側へ移して
ホスト側の sh スクリプトなしでチェーン全体を再ビルドする。
