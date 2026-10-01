<!-- SPDX-License-Identifier: Apache-2.0 -->

# ファイル別ライセンス監査

この文書は、世代順に確認した出自と SPDX 表記を記録する。`Apache-2.0` は
stone で新たに書いた部分に適用する。外部プログラムを入力として取得することと、
そのソースをリポジトリへ取り込むことは区別する。機能、API 名、ABI 定数の
一致だけではソースの流用と判定しない。一方、由来が不明なファイルには
推測で `Apache-2.0` を付けない。

`*.bin` のようにコメントを書けないファイルには、同じパスに `.license` を
加えた sidecar で SPDX 識別子を記録する。ソース形式のコメントは、
実行時に入力として扱われるかを確認してから追加する。

## 確認済み: Stage 0–2

| ファイル | 出自 | SPDX |
|---|---|---|
| `docs/plan.md` | stone の計画書。既存 bootstrap コードの流用を禁止する方針を記載 | `Apache-2.0` |
| `docs/stage001-hex0.md`, `stage001/hex0.md` | stone の設計・説明文書 | `Apache-2.0` |
| `stage001/hex0.bin`, `stage001/hex0.hex` | 人手で符号化した seed と同じ命令列の listing | `Apache-2.0` |
| `tests/stage000/hello.bin`, `tests/stage000/hello.md`, `tests/stage000/test.sh` | 人手で符号化した疎通確認用プログラムと stone のテスト・説明文書 | `Apache-2.0` |
| `tests/stage001/hello.hex`, `tests/stage001/test.sh` | Stage 0 の命令列の再記述と stone のテスト | `Apache-2.0` |
| `docs/stage002-hex1.md`, `stage002/hex1.hex`, `stage002/hex1.md` | stone の hex1 実装・文書 | `Apache-2.0` |
| `tests/stage002/labels-expected.hex`, `tests/stage002/labels.hex1`, `tests/stage002/test.sh` | stone のテスト入力・期待値・テスト | `Apache-2.0` |

根拠: Stage 0 の seed は `tests/stage000/hello.md`、Stage 1 は
`stage001/hex0.md` と導入コミット `1994ab3bc6bce99ca1b396c98315b4912d64fb92`、
Stage 2 は `stage002/hex1.md` と導入コミット
`6f4b35e`。現行内容と導入時の差分を照合した。これらのファイルに
Linux や glibc のソースを取り込んだ記録は見つからなかった。

## 確認済み: Stage 3–10 の実装と文書

| 世代 | 対象ファイル | 出自 | SPDX |
|---|---|---|---|
| 3 | `stage003/asm.hex1`, `stage003/asm.md`, `docs/stage003-asm.md` | stone のアセンブラと設計 | `Apache-2.0` |
| 4 | `stage004/sol.s`, `stage004/sol.md`, `docs/stage004-sol.md` | stone の小言語処理系と設計 | `Apache-2.0` |
| 5 | `stage005/sc.sol`, `stage005/sc.md`, `docs/stage005-sc.md` | stone の C サブセット処理系と設計 | `Apache-2.0` |
| 6 | `stage006/scc.sc`, `stage006/scc.md`, `docs/stage006-scc.md` | Stage 5 を stone の言語で再記述 | `Apache-2.0` |
| 7 | `stage007/occ.sc`, `stage007/occ.md`, `docs/stage007-occ.md` | Stage 6 を発展させた stone の処理系 | `Apache-2.0` |
| 8 | `stage008/cc.sc`, `stage008/cc.md`, `stage008/ld.sc`, `stage008/ld.md`, `docs/stage008-elf-ld.md` | stone の ELF オブジェクト生成器とリンカ | `Apache-2.0` |
| 9 | `stage009/pp.sc`, `stage009/pp.md`, `docs/stage009-pp.md` | stone のプリプロセッサ | `Apache-2.0` |
| 10 | `stage010/cc.sc`, `stage010/cc.md`, `stage010/cc2.sc`–`cc12.sc`, 対応する `cc2.md`–`cc12.md`, `stage010/include/stdarg.h`, `docs/stage010-c89.md` | stone の C89 処理系の改訂系列、ABI 用ヘッダと設計 | `Apache-2.0` |

根拠: Stage 3–9 の導入コミットは順に `d001ee5`, `5a383ff`,
`0a62ae7`, `879f4dd`, `8438424`, `bee801a`, `753bf69`。
Stage 10 は `9f3b5bc` を起点とする改訂系列である。各世代の文書、
ソース冒頭の説明、導入履歴を確認した。ELF 形式や C89 の仕様への準拠は、
その実装コードを既存処理系から引用したことを意味しない。

## 確認済み: Stage 11–14

| 世代 | 対象ファイル | 出自と判定 | SPDX |
|---|---|---|---|
| 11 | `stage011/` 以下の全 12 ファイル、`docs/stage011-libc.md` | C89 の仕様に沿って stone 向けに書いた libc の関数・ヘッダと設計文書 | `Apache-2.0` |
| 12 | `stage012/` 以下の全 25 ファイル、`docs/stage012-os.md` | Stage 11 の libc を継承し、stone のカーネルと環境部を追加 | `Apache-2.0` |
| 13 | `stage013/` 以下の全 38 ファイル、`docs/stage013-tools.md` | Stage 12 を継承し、stone のゲスト用ツール等を追加 | `Apache-2.0` |
| 14 | `stage014/` 以下の全 44 ファイル、`docs/stage014-external.md` | stone のコンパイラ・libc・カーネルの改訂と設計文書 | `Apache-2.0` |

これら 119 ファイルはすべて個別の `.license` sidecar を持つ。世代を凍結した
ビルド入力を変更しないため、ソース本文には SPDX コメントを挿入しない。
Stage 12–14 が使う RV32 Linux 互換 syscall 番号、errno、open フラグは
インターフェースの値である。確認した実装には Linux カーネルや glibc の
ソース本文を取り込んだ記録や著作権表示がない。C 標準・POSIX の関数名、
動作、定数値を合わせたことだけを理由に、実装ファイルへ Linux や glibc の
ライセンスを付けない。疑わしい一致が後に見つかれば、そのファイルの
出自を再検証する。

Stage 14 でビルド対象とした bzip2 / zlib の原本は
`docs/external/` に取得する。ここで対象とした `stage014/` のファイルは
それらの未改変原本ではなく、自作処理系とその周辺である。

## 確認済み: Stage 15–17

| 世代 | 対象ファイル | 出自と判定 | SPDX |
|---|---|---|---|
| 15 | `stage015/` 以下の 115 ファイルのうち下記 3 ファイルを除く 112 ファイル | stone の C 処理系、OS、実行環境 | `Apache-2.0` |
| 15 | `stage015/tcc/riscv32.patch` | TinyCC の固定 commit `2ba12e83b3599ca8f5d50c179fe5138fe956f0c9` への差分。上流コードの文脈行を含む | `LGPL-2.1-only` |
| 15 | `stage015/cc15n.md`, `stage015/cc15o.md` | stone の説明文書。TinyCC `tccgen.c` 等のコード抜粋には個別の snippet 注記 | 本文 `Apache-2.0`、引用部分 `LGPL-2.1-only` |
| 15 | `docs/stage015-riscv32.md`, `docs/stage015-tcc.md` | stone の分析文書。TinyCC 上流コードの抜粋には個別の snippet 注記 | 本文 `Apache-2.0`、引用部分 `LGPL-2.1-only` |
| 16 | `stage016/` 以下の全 91 ファイル、`docs/stage016-os.md` | Stage 15 の stone 実装を継承した OS と libc の改訂 | `Apache-2.0` |
| 17 | `stage017/` 以下の 251 ファイルのうち `pp18.md` を除く 250 ファイル | stone の処理系、libc、シェル、sed・awk 等の自作実装と測定記録 | `Apache-2.0` |
| 17 | `stage017/pp18.md` | stone の説明文書。GCC `libcpp/system.h` のマクロには個別の snippet 注記 | 本文 `Apache-2.0`、引用部分 `GPL-3.0-or-later` |
| 17 | `docs/stage017-cc.md` | stone の分析文書。TinyCC 上流コードの抜粋には個別の snippet 注記 | 本文 `Apache-2.0`、引用部分 `LGPL-2.1-only` |
| 17 | `docs/stage017-gcc.md` | stone の分析文書。GCC 上流コードの抜粋には個別の snippet 注記 | 本文 `Apache-2.0`、引用部分 `GPL-3.0-or-later` |

各ファイルには `.license` sidecar を付け、入力の内容は変更しない。
上記 7 文書では sidecar の `Apache-2.0` は自作の本文に適用し、
上流コードの抜粋には `SPDX-SnippetBegin` / `SPDX-SnippetEnd` で
個別に上流ライセンスを示す。引用部分へ Apache-2.0 を付与したものではない。
`riscv32.patch` はパッチ前の TinyCC のコードも含むため、上流の
`COPYING` (LGPL 2.1) を保守的に `LGPL-2.1-only` と記録し、
全文を `LICENSES/LGPL-2.1-only.txt` に置く。追加した RV32 実装も
このパッチでは同じ条件にする。TinyCC の原本は `docs/external/` に
取得され、リポジトリ内に丸ごと複製されない。

## 共通コード・テスト・Web

| 対象 | 出自と判定 | SPDX |
|---|---|---|
| `.gitattributes`, `.gitignore`, `.github/`, `env/`, `tools/`, `verify/`, `web/` | stone の設定、構築・検証用実装、デモ。`verify/audit/` は仕様から独立に書き起こした実装 | `Apache-2.0` |
| `docs/SOURCES.md`, `docs/artifacts.md`, `docs/dev-notes.md`, `docs/roadmap.md` | stone の記録・設計文書 | `Apache-2.0` |
| `tests/hostshim/`, `tests/lib.sh`, `tests/stage003/`–`tests/stage017/`（下記の例外を除く） | stone の検証入力、期待値、測定結果、テストハーネス。外部ライブラリをリンク・実行する検証用 C ファイルも stone 側で記述 | `Apache-2.0` |
| `tests/stage017/fixtures/gcc47-defined.c` | GCC 4.7.4 の `gcc/system.h` のマクロを基にした検証入力。元の引用を `test.sh` から分離 | `GPL-3.0-or-later` |
| `tests/stage016/refbin/`, `td/`, `a.o`, `b.o`, `c.o` | stone の比較用スクリプトとテスト用データ。`.o` は名前に反して短い文字データ | `Apache-2.0` |

テスト入力や期待値は、コメントを加えると検証結果が変わる場合がある。
これらには `.license` sidecar を使い、元の内容を維持する。
`tests/stage017/fixtures/gcc47-defined.c` は GCC の `gcc/system.h` に
由来するため、上流の著作権表示を sidecar に保存し、上流の
`COPYING3` を `LICENSES/GPL-3.0-or-later.txt` に収める。
ほかのテスト入力にある外部プログラムの関数名・定数・出力統計は、
外部ソース本文の転載と区別している。

CI の `license` ジョブは追跡ファイルの SPDX 表記または sidecar を検査する。
外部資料の実体は `docs/external/` (git ignore) へ取得し、リポジトリには
格納しない。外部資料のライセンスを stone のソースへ自動的に引き継がせない。
