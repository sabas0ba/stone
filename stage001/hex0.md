# hex0.bin 説明文書

`stage001/hex0.bin` は人手で命令エンコードした生のバイナリであり，
ビルドによって生成されない ([plan.md](../docs/plan.md) 2.1)。仕様は [stage001-hex0.md](../docs/stage001-hex0.md) を参照。

- 対象: RV32I，リトルエンディアン，64 命令 256 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
- 動作: UART から hex0 言語のテキストを読み，バイナリを UART へ書き出すフィルタ

SHA-256: 4ea1781a7c450eb953dbde1af2f93301f631e62a162aaa54ea6082d370ff5cac

## 命令 listing

全命令の注釈付き listing は `stage001/hex0.hex` に置く。同ファイルは hex0 言語で
記述された hex0 自身のソース表現でもあり，listing とバイナリの整合は
自己再生成テスト `hex0(hex0.hex) == hex0.bin` で機械的に検証される
(tests/stage001/test.sh)。listing を本文書へ複製すると二重管理となるため行わない。

## プログラム構造

| アドレス | ラベル | 内容 |
|---|---|---|
| 0x80000000 | (init) | UART base (x5), finisher base (x6), 桁状態 (x9) の初期化 |
| 0x8000000c | main_loop | LSR.DR ポーリングによる 1 文字受信 |
| 0x8000001c | | 16 進数字の範囲判定 ('0'..'9' / 'a'..'f') |
| 0x80000040 | dig09/digaf | 文字から 4 bit 値への変換 |
| 0x8000004c | have_digit | 桁状態により上位桁格納または下位桁合成へ分岐 |
| 0x8000005c | low_digit | バイト完成。LSR.THRE ポーリング後 THR へ出力 |
| 0x80000078 | not_hex | 非 16 進文字の分類 (コメント開始/空白/終端/エラー) |
| 0x800000b0 | nh_low | 桁間への他文字混入 ('.' は err2，他は err1) |
| 0x800000bc | comment | LF まで読み飛ばし |
| 0x800000d8 | pass/err1/err2 | 終了値 (0x5555 / 0x13333 / 0x23333) の設定 |
| 0x800000f8 | do_exit | test finisher への書込み |

## 検証

- 自己再生成・交差検証・エラー系: tests/stage001/test.sh
- 逆アセンブルによる照合 (verify 層): `sh verify/disasm.sh stage001/hex0.bin`
