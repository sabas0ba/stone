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

## 継続調査

- Stage 3 以降の実装、検証用ファイル、共通スクリプト、Web 表示用ファイルは
  個別の出自確認を続ける。未確認のファイルに SPDX を一括適用しない。
- Stage 12 以降の Linux 互換 syscall 番号と errno の定数は、参照した
  インターフェースとソース表現の流用を分けて確認する。
- `stage015/tcc/riscv32.patch` は固定された TinyCC 上流ソースへの
  差分であり、上流の文脈行も含む。TinyCC の `README` と `COPYING` は
  LGPL 2.1 を示す。パッチ全体の識別子と上流の著作権表示は、
  対象ファイルの条項を確認してから付ける。
- `docs/external/` の zlib、bzip2、TinyCC、GCC などは取得する入力であり、
  リポジトリには格納しない。各上流のライセンスを stone のソースへ
  自動的に引き継がせない。
