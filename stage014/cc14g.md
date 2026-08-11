# cc14g 説明文書

cc14g は C コンパイラの Stage 14 第 9 部の世代である。
[cc14f.sc](cc14f.sc) (第 8 部) を出発点に，**zlib 1.3.1 を当てて
落ちた箇所**を潰した。設計は
[stage014-external.md](../docs/stage014-external.md) 13 章。

ソース [cc14g.sc](cc14g.sc) が正本である。

## ビルド

```
sh tools/build.sh stage014
# cc14f(cc14g.sc) -> cc14g0     (1 段目)
# cc14g0(cc14g.sc) -> cc14g     (正本。以降は固定点)
```

SHA-256: c0cd490dd56b3e5b4b7aaf6a11ae7e045bbc0dbf625be96aacb3e125addcbc89

- 対象: RV32IM，リトルエンディアン，156652 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## cc14f との差分 (すべて zlib の実測で見つかったもの)

| 差分 | 台帳 | zlib での出所 |
|---|---|---|
| ポインタ修飾の後置 const (`char * const`) | ptrconst | z_errmsg |
| 大域初期化子のキャスト (`(char *)"..."`) | castinit | z_errmsg・static_bl_desc |
| 大域初期化子の定数式 (`{ tab, 256 + 1 }`) | castinit | static_l_desc |
| 関数内 static の初期化子 | staticinit | inflate_table の表・inffixed.h |
| case ラベルの定数式 (列挙定数) | caseconst | inflate の状態機械 |
| typedef 形式の構造体定義の入れ子 struct 登録 | opaqueptr | z_stream / deflate_state / ct_data |

### opaqueptr の根因 (1 箇所のバグ・3 つの症状)

`ptype()` の struct 分岐はタグ名を大域バッファ `snam` に置いたまま
`strudef()` を呼んでいた。メンバの型に `struct X` が現れると
(無名 struct/union・タグ形のメンバ宣言・前方参照の登録のいずれでも)
入れ子の `ptype()` が snam を上書きし，戻ってからの `sfind2()` が
**内側の struct を返す**。typedef はその誤った番号を記録するので，
以降その typedef 名でのメンバ参照が全て型エラー (5) になっていた。

同じ構造をタグ形 (`struct z_stream_s *`) で書くと毎回引き直すので
通る，という非対称が診断の鍵だった。修正は strudef の前後で snam を
ローカルへ退避・復元するだけである。

### 実装の要点

- **`* const` は読み飛ばす。** 修飾子を検査に使わない本処理系では
  先頭の const と同じ扱いでよい (pstars)
- **case と初期化子の値は畳込み評価器 `ccond` で読む** (cc14f で宣言子に
  入れたものと同じ)。初期化子ではキャストを読み飛ばし，書く幅は
  対象の型が決める
- **関数内 static の初期化子は大域と同じ `ginit` で書く。** 実体の
  置き場が .bss から .text 内のデータへ変わるだけである。文字列
  リテラルの flush (spool は関数単位) だけ関数末尾へ遅らせる (ginfn)

## 検証

- 固定点 B2 == B3
- 回帰: sh / ed / mk を cc10l と同じ .o にコンパイルする (コード生成は
  1 バイトも変えていない)
- 台帳 32 ok / 3 gap / 0 bad
- **無改変の zlib 1.3.1 コア 11 ファイル** (adler32 / crc32 / zutil /
  inftrees / inffast / inflate / infback / trees / deflate / compress /
  uncompr) が pp14 + cc14g を通り，kernel13 上で crc32 / adler32 と
  deflate -> inflate の往復一致。圧縮出力はホストの zlib が伸長でき，
  チェックサムも一致する

## 残っている未対応

台帳を参照。浮動小数点・空の引数リストへの実引数・long long が残る。
いずれも zlib の Z_SOLO 構成には不要である。
