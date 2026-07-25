# scc 説明文書

scc は sc コンパイラの sc 言語による再記述 (セルフホスト) である。
設計は [stage006-scc.md](../docs/stage006-scc.md)，言語仕様は
[stage005-sc.md](../docs/stage005-sc.md) 2 章を参照。

ソース [scc.sc](scc.sc) が以降の正本である。バイナリはビルドで再現される
生成物であり，git 管理しない:

- scc1.bin (B1): Stage 5 の sc コンパイラで bootstrap したもの
- scc.bin (B2): scc が自分自身をコンパイルしたもの (以降の実行に用いる正本)

## ビルド

```
sh tools/build.sh stage006
# hex0 -> hex1 -> asm -> sol -> sc -> scc1 (B1) -> scc (B2) の順で生成
```

ビルドはビット再現であり，B2 (scc.bin) の SHA-256 は以下と一致しなければならない。

SHA-256: 5bb7ae24f0405ff1874522da72fdf99cf0b8fae0f1215ad46ba584f6bda5bbf0

- 対象: RV32IM，リトルエンディアン，74296 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
- コード生成が Stage 5 実装と同一のため B1 == B2 であり，記載値は 1 つである。
  固定点 B2 == B3 = B2(scc.sc) が完了条件である ([plan.md](../docs/plan.md) 4 章)。

## プログラム構造 (scc.sc)

Stage 5 実装 (sc.sol) の構成を sc 言語へ移植したもので，関数名・役割は
[sc.md](../stage005/sc.md) の構造表とほぼ対応する。sc 言語の制約による相違:

- 記号表は並行配列 (gname/gkind/gty/gval/gdef/garr/gna 等)。未発見は -1
- トークン種別等の定数は大域変数として init() で設定
- 出力バッファ ob への語アクセスは int ポインタ変数 wp を経由
- 名前比較・複写は streq/copyn (16 バイト固定スロット)
- 組込み関数の登録は文字列リテラルを用いる biadd("getc", ...) 等

## 検証

- ビルド再現・固定点 (B2 == B3, B1 == B2)・Stage 5 仕様スイートとの同値性・
  エラー系: tests/stage006/test.sh
- 逆アセンブルによる照合 (verify 層): `sh verify/disasm.sh tmp/build/scc.bin`
