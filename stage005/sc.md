# sc 説明文書

sc は，C サブセット言語 sc のソーステキストをフラットバイナリへ変換する
コンパイラである。言語仕様は [stage005-sc.md](../docs/stage005-sc.md) 2 章を参照。

sc のバイナリ (sc.bin) は sol によるビルドで再現される生成物であり，git 管理しない。
ソースは [sc.sol](sc.sol) (sol 言語) である。

## ビルド

```
sh tools/build.sh stage005    # hex0 -> hex1 -> asm -> sol -> sc の順で tmp/build/sc.bin を生成
```

ビルドはビット再現であり，生成物の SHA-256 は以下と一致しなければならない。

SHA-256: ece275e324fc29afbf1290c59703b8210a12e9dd67427598245f9bf43926cdec

- 対象: RV32IM (生成コード・sol 生成コードとも mul/div/rem を含み得る)，
  リトルエンディアン，57356 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## プログラム構造 (sc.sol)

| 部分 | 内容 |
|---|---|
| getch / adv / is* / esc* | 入力の 1 文字操作と文字種判定 |
| skipwc / lexnum / lexid / lexchr / lexstr / lexop / next | 字句解析 (トークン: 数値・識別子・文字列・演算子。予約語は lexid で判定) |
| cur / outw / outb / patw / getw | 出力バッファ操作 (バックパッチ含む) |
| jenc / benc | J/B-type 即値の合成 |
| epop / epush / elit / eswp / ebin / ebin2 / eload / estore / escale / ediv / eladdr / eoffs / eepilog | コード生成テンプレート |
| gfind / gnew / lfind / lnew / sfind / sfind2 / mfind / patchcalls / ecall | 記号表 (大域・ローカル・構造体・メンバ) と前方参照呼出しの解決 |
| ptype / pstars / tsize / bytesz | 型の解析とサイズ計算 |
| rv / eprim / eident / ecallseq / epost / euna / emul / eadd / edoadd / eshift / erel / eeq / eband / exor / ebor / ecand / ecor / expr | 再帰下降の式解析 (優先順位順)。edoadd はポインタ演算のスケーリング |
| stmt | 文 (ブロック・if/else・while・return・式文) |
| memb / structdef / funcdef / dcont / topdecl | 宣言 (構造体・大域変数・関数。ローカルは funcdef 内) |
| bi1 / bireg | 組込み関数 (getc/putc/exit) の登録 |
| main | 駆動部: 入力読込み (EOT まで)・ランタイム前置部の出力・解析・main の解決検査・未解決関数の検査・バッファの書出し |

メモリマップ (コンパイラ実行時・生成コードの規約) は
[stage005-sc.md](../docs/stage005-sc.md) 3 章を参照。

## 検証

- ビルド再現・仕様テストスイート (arith/fib/ptr/struct)・フィルタ (upper)・
  エラー系: tests/stage005/test.sh
- 逆アセンブルによる照合 (verify 層): コンパイル出力を
  `sh verify/disasm.sh tmp/arith.bin` 等で逆アセンブルし，ランタイム前置部
  (sc.sol の main 冒頭 32 語) とテンプレート (e* 各関数) と照合する。
