# asm 説明文書

asm は，RV32IM のニーモニック・疑似命令・ラベルで書かれたソーステキストを
フラットバイナリへ変換するアセンブラである。仕様は
[stage003-asm.md](../docs/stage003-asm.md) を参照。

asm のバイナリ (asm.bin) は hex1 によるビルドで再現される生成物であり，git 管理しない。
ソースは [asm.hex1](asm.hex1) (hex1 言語) で，全行に asm 言語の転記コメント (`#:`) を持つ。
転記を機械抽出したソースを asm 自身で処理すると asm.bin とビット一致する
(自己アセンブル固定点。tests/stage003/test.sh)。

## ビルド

```
sh tools/build.sh stage003    # hex0 -> hex1 -> asm の順で tmp/build/asm.bin を生成
```

ビルドはビット再現であり，生成物の SHA-256 は以下と一致しなければならない。

SHA-256: 50dca9893143542ed5a557aa203cf13456ff02eed8914902a0982f9a4f58e8db

- 対象: RV32I (実装自体は I のみ使用)，リトルエンディアン，3528 バイト (0xdc8)
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## プログラム構造

コードはラベル参照 (hex1 の `!` `$` `&`) で結線されており，hex0 で書かれた hex1 の
ようなブロック整列・パディングを持たない。主要ルーチンは以下の順に並ぶ。

| ルーチン | 内容 |
|---|---|
| init / getmt | 定数レジスタ設定。`auipc`+`lw` でニーモニック表アドレスを取得 |
| p0_* | pass0: UART から入力バッファへ (コメント内の `.` は終端としない) |
| scan_* / s_* | pass1 (アドレス計算) / pass2 (出力) 共用の走査本体。ラベル定義とニーモニック表探索 |
| skipws | 空白 (SP TAB CR LF `,`) とコメントの読み飛ばし |
| parse_word | 名前の読取り (長さ 1 + 名前 15 を scratch へ) |
| sym_lookup | シンボル表の線形探索 |
| parse_reg / parse_imm / parse_mem | レジスタ (`x0`..`x31`)・即値 (10 進/16 進)・`(reg)` の解析 |
| lab_addr | ラベルオペランドの解決 (pass2 のみ。未定義は err3) |
| ck_* | 即値範囲検査 (imm12 / shamt / imm20 / byte。範囲外は err6) |
| h_* | 形式ハンドラ (R/I/SH/LD/S/B/U/J/FX/word/byte と疑似命令) |
| pair_emit | li/la の lui+addi 対の合成 |
| b_merge / j_merge | B/J-type 即値ビットの散在配置 (範囲外・奇数は err5) |
| emit_word / emit_byte | 出力 (pass2 のみ UART へ。出力アドレスは常に前進) |
| err* / exit_ok | 終了コード (0, 1..7) の設定と test finisher 書込み |
| mtable | ニーモニック表 (24 バイト/エントリ × 60 + 終端)。base word とハンドラアドレス |

メモリマップ: scratch 0x800f_0000，シンボル表 0x8010_0000 (20 バイト/エントリ)，
入力バッファ 0x8020_0000〜 (hex1 と同一)。

## 検証

- ビルド再現・全命令エンコード・自己アセンブル固定点・実行・エラー系:
  tests/stage003/test.sh
- 逆アセンブルによる照合 (verify 層):
  `sh verify/disasm.sh tmp/build/asm.bin` の出力を asm.hex1 の転記コメントと照合する。
  全命令エンコードテストの出力も同様に照合できる
  (`sh verify/disasm.sh tmp/rv32im-asm.bin`)。
