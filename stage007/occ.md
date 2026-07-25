# occ 説明文書

occ は現代化した sc コンパイラである: 構文解析は 3 番地コードの IR (φ なし SSA)
を構築し，最適化パス (定数畳み込み・不要コード削除) が IR を書き換え，
線形走査のレジスタ割付けを経てコードを出力する。設計は
[stage007-occ.md](../docs/stage007-occ.md)，言語仕様は
[stage005-sc.md](../docs/stage005-sc.md) 2 章 (変更なし) を参照。

ソース [occ.sc](occ.sc) が以降の正本である。バイナリはビルドで再現される
生成物であり，git 管理しない:

- occ1.bin (B1): scc で bootstrap したもの (旧コード生成による occ)
- occ.bin (B2): occ が自分自身をコンパイルしたもの (以降の実行に用いる正本)

コード生成の質の違いから B1 (126788 バイト) と B2 (70420 バイト) は
一致しない。固定点 B2 == B3 = B2(occ.sc) が完了条件である。

## ビルド

```
sh tools/build.sh stage007
# ... -> sc -> scc1 -> scc -> occ1 (B1) -> occ (B2) の順で生成
```

ビルドはビット再現であり，B2 (occ.bin) の SHA-256 は以下と一致しなければならない。

SHA-256: 253cfd88361001c4922476aae2fa7632efe744c2138eeccdfa1f384c05cc036d

- 対象: RV32IM，リトルエンディアン，70420 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## プログラム構造 (occ.sc)

字句解析・記号表・宣言の解析は Stage 6 実装 (scc.sc) を継承する。
新設部分は以下のとおり。

| 部分 | 内容 |
|---|---|
| iop/ia/ib ほか | 関数単位の IR (3 番地コード)。値は定義命令の番号で参照する |
| emit / newlab / hslot | IR 構築 (式は値 id を返す。&&/\|\| は隠しスロット経由) |
| foldins / fold | 定数畳み込み (BIN/NEG/NOT，定数条件の BZ/BNZ の JMP 化・削除) |
| markv / dce | 不要コード削除 (未使用の純粋な値の削除。ロードも純粋とみなす) |
| usemark / regalloc | 線形走査レジスタ割付け (x13..x27，全て callee-saved。空きがなければ新規値をスピル) |
| rw3 / iw3 / sw3 / liw | 命令語の組立て (小さい定数は addi 1 語) |
| oreg / dreg / dstore / lrefj | オペランド取出し・スピル書戻し・ラベル参照 (分岐は逆条件 + jal の 2 語で距離制限なし) |
| emitins / epilog2 / emitfn | 命令単位の出力，エピローグ，関数全体の出力 (プロローグでの使用レジスタ退避，ラベル・文字列プールの後埋め) |

生成コードの ABI (データスタックによる引数・返却値の受渡し，ランタイム
前置部 32 語) は scc と同一である。

## 検証

- ビルド再現・固定点 (B2 == B3)・Stage 5 仕様スイートとの同値性・
  パス単体テスト (fold/dce のビット一致，出力サイズ改善)・エラー系:
  tests/stage007/test.sh
- 逆アセンブルによる照合 (verify 層): `sh verify/disasm.sh tmp/build/occ.bin`
