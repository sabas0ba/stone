# hex1 説明文書

hex1 は，ラベルの定義・参照によって分岐オフセットの手計算を不要にする
hex テキスト → バイナリ変換フィルタである。仕様は [stage002-hex1.md](../docs/stage002-hex1.md) を参照。

hex1 のバイナリ (hex1.bin) は hex0 によるビルドで再現される生成物であり，git 管理しない。
ソースは [hex1.hex](hex1.hex) (hex0 言語) で，全命令の注釈付き listing を兼ねる。

## ビルド

```
sh tools/build.sh stage002    # tmp/build/hex1.bin を生成
```

ビルドはビット再現であり，生成物の SHA-256 は以下と一致しなければならない。

SHA-256: 827550b2729be1d6540dc48845fc7627f9293059f59132dd423f839180a88552

- 対象: RV32I，リトルエンディアン，2128 バイト (0x850)
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## プログラム構造

| アドレス | ブロック | 内容 |
|---|---|---|
| +0x000 | init+pass0 | レジスタ初期化。UART から入力全体を RAM バッファへ格納 (コメント内の `.` は終端としない) |
| +0x100 | scanner | pass1 (アドレス計算) / pass2 (出力) 共用の走査本体と文字種分岐 |
| +0x200 | h_colon | `:` ラベル定義 (pass1 のみ登録，重複は err4) |
| +0x280 | h_amp | `&` ラベル絶対アドレスの LE 4 バイト出力 |
| +0x300 | h_bang | `!` B-type 参照: template へ imm[12:1] を合成 (範囲外は err5) |
| +0x400 | h_dollar | `$` J-type 参照: template へ imm[20:1] を合成 (範囲外は err5) |
| +0x500 | parse_name | 名前の読取りと scratch への格納 (長さ 1..15) |
| +0x600 | lookup | シンボル表の線形探索 (16 バイトを 4 語比較) |
| +0x680 | hexval | 16 進数字の値化 (非 16 進は err1 へ直行) |
| +0x700 | read_byte | template 用の hex ペア読取り (空白・コメント許容) |
| +0x780 | emit_byte | 1 バイト出力 (pass2 のみ UART へ書く。出力アドレスを前進) |
| +0x7c0 | emit_word | 32 bit 語の LE 4 バイト出力 |
| +0x800 | exit/err | 終了コード (0, 1..5) の設定と test finisher 書込み |

メモリマップ: シンボル表 0x8010_0000 (20 バイト/エントリ)，入力バッファ 0x8020_0000〜。
scratch (名前組立て) 0x800f_0000。

## 検証

- ビルド再現・上位互換・自己ビルド・ラベル機能・エラー系: tests/stage002/test.sh
- 逆アセンブルによる照合 (verify 層): `sh verify/disasm.sh tmp/build/hex1.bin`
