# sol 説明文書

sol は，スタック指向の小言語 sol のソーステキストをフラットバイナリへ変換する
コンパイラである。言語仕様は [stage004-sol.md](../docs/stage004-sol.md) を参照。

sol のバイナリ (sol.bin) は asm によるビルドで再現される生成物であり，git 管理しない。
ソースは [sol.s](sol.s) (asm 言語) である。

## ビルド

```
sh tools/build.sh stage004    # hex0 -> hex1 -> asm -> sol の順で tmp/build/sol.bin を生成
```

ビルドはビット再現であり，生成物の SHA-256 は以下と一致しなければならない。

SHA-256: 079d90e9accfb4309c31e1ba82835d9fa25c7ef35db0acaa4154181a8376dad1

- 対象: RV32I (実装自体は I のみ使用。生成コードは M 拡張の mul/div/rem を含む)，
  リトルエンディアン，5604 バイト (0x15e4)
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## プログラム構造

| ルーチン | 内容 |
|---|---|
| start / p0_* | 定数レジスタ設定。pass0: 入力バッファへの読込み (コメント・文字列・文字リテラル内の `.` は終端としない) |
| pass_init / ep_* | パス毎の初期化とランタイム前置部 (33 語) の出力。語 4 は `jal x1 main` に置換 |
| scan_loop / s_* | pass1/pass2 共用の走査本体。数値・文字・文字列・語の分類と記号解決 |
| skipws / parse_word / parse_num / tok_term | 字句解析 |
| prim_lookup / sym_lookup / sym_add / dup_check | プリミティブ表・記号表の探索と登録 |
| ctl_push / ctl_pop | 制御スタック (if/begin/while の対応検査) |
| pop_emit / lit_push / j_emit / b_emit | コード生成 (条件 pop・リテラル push・J/B 即値合成) |
| emit_word / emit_byte | 出力 (pass2 のみ UART へ) |
| err* / exit_ok | 終了コード (0, 1..7) の設定と test finisher 書込み |
| h_* | 語ハンドラ (定義・制御構造・ランタイム呼出し・テンプレート展開) |
| pre_tmpl / t_* / ptable | ランタイム前置部・展開テンプレート・プリミティブ表 (28 バイト/エントリ) |

メモリマップ (コンパイラ実行時): 文字列バッファ 0x800e_0000，scratch 0x800f_0000，
記号表 0x8010_0000 (32 バイト/エントリ)，制御スタック 0x801c_0000，
fixup 表 0x801e_0000，入力バッファ 0x8020_0000〜。

生成プログラム実行時: データスタック 0x87f0_0000 (下向き，x9)，
リターンスタック 0x8780_0000 (下向き，x2)，大域変数 0x8010_0000〜。

## 検証

- ビルド再現・実行 (hello/fib/ctrl)・フィルタ (echo)・エラー系: tests/stage004/test.sh
- 逆アセンブルによる照合 (verify 層): コンパイル出力を
  `sh verify/disasm.sh tmp/hello.bin` 等で逆アセンブルし，ランタイム前置部と
  テンプレート (sol.s の pre_tmpl / t_*) と照合する。
