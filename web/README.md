# web: ブートストラップの各段階を確認する GitHub Pages

各 Stage の仕様、成果物、実行例を表示する静的 Web アプリ。
スライダーで Stage 1〜16 を選択し、実装された機能と成果物を確認できる。
実行に使うバイナリは `tools/build.sh` の生成物であり、
ブラウザ内の RV32IM エミュレータ (`rv32.js`) で動作する。
入力した hex テキストは hex0 が変換し、C ソースは各世代のコンパイラが処理する。

## 機能一覧と画面操作

「能力の推移」(Capability growth) は、10 系統 × 16 世代の表で追加機能を示す。
コンパイラの機能と実行環境・OS の機能を色分けし、成果物のサイズも同じ表に表示する。

- セルを押すと、その世代・系統の追加機能を表の下に表示する。もう一度押すと閉じる。
- 行名を押すと、kernel12〜kernel18、cc15a〜cc15p、ld〜ld16 などの内訳を表示する。

画面上部に機能一覧、Stage の概要、実行ボタンを配置する。
1180 × 900 の画面で測定した配置では、Run は y=845、Boot は y=878 にあり、
スクロールせず操作できる。画面の高さを抑えるため、次の構成にしている。

- Run と Boot を入力欄の上に置き、実行シナリオの選択と Boot を同じ行に並べる。
- `reads` / `written in` / `built by` は 1 行で省略表示し、title 属性に全文を設定する。
- 追加機能のチップ (`new_capabilities`) は機能一覧と重複するため、初期状態では閉じる。

## プレイグラウンドとターミナル

Stage 14 / 15 では、`tests/stageNNN/ledger.txt` に記録した適合テストを選択できる。
選んだテストの状態 (ok / gap / bad) と期待値を表示し、実行結果と比較する。
`gap` は未対応機能を含み、コンパイラが指定の非ゼロ終了コードで拒否することを検査する。

Stage 12 / 13 / 15 / 16 では、kernel と sfs イメージを起動して対話操作できる。

- **Stage 12 / 13**: Stage 12 は起動時に指定したプログラムを実行する。
  Stage 13 はシェル、ed、cc / ld / pp を使い、編集、コンパイル、実行、
  cc 自身の再ビルドを行える。操作例は Guided tour に表示する。
  終了後は sfs 内のファイル (`cc10l.bin` を含む) をダウンロードできる。
- **Stage 15**: kernel16 上で `lib15` を実行し、`%llu` / `%f`、
  `snprintf`、`strtod`、`sscanf`、`setjmp`、`lseek` の動作を確認する。
  ベアメタルのプレイグラウンドには printf がないため、そちらではビット列を出力する。
- **Stage 16**: 4 つの実行シナリオで、kernel17 の経路解決、kernel18 の
  ディレクトリ操作、kernel18 / kernel19 のメモリ割当容量を確認する。
  同じメモリ検査を両カーネルで実行し、割当可能量を比較する。
  実行後は sfs2 のディレクトリツリーを表示し、`dirprobe` が作成した
  `out/` などを確認できる。kernel19 は配置アドレスが 0xa000_0000 まで及ぶため、
  エミュレータに 512 MB のメモリを確保する。

## 構成

| ファイル | 内容 |
|---|---|
| rv32.js | RV32IM エミュレータ。UART + test finisher (docs/plan.md 3 章) に加え，M/U 特権・CSR・mret・ecall トラップ (ld12/ld13 の 'K' 前置部と kernel が使う範囲。docs/stage012-os.md 5 章)。入力が尽きると停止し，足すと続きから走る (対話実行)。RAM の大きさは指定できる (既定 128 MiB / kernel19 は 512 MiB) |
| sfs.js | sfs イメージの構築・読出し (tools/sfs.sh の JS 版。docs/stage012-os.md 4 章) |
| sfs2.js | sfs2 (ディレクトリを持つ) の構築・読出し (tools/sfs2.sh の JS 版。docs/stage016-os.md 6.3)。項目の並びまで同じにしてあり，同じ木からは同じ像が出る |
| worker.js | 実行係 (Web Worker)。パイプラインと OS セッション (ターミナル) の両方 |
| pipelines.js | 各世代のパイプライン定義とターミナル定義 (tools/build.sh・tests/ と同じ手順の再現) |
| app.js / index.html / style.css | UI 本体 (世代スライダー・能力の推移の格子 + 成果物の大きさ・要約・成果物・プレイグラウンド・ターミナル) |
| data/stages-content.json | 世代コンテンツ (仕様要約・新機能・成果物説明) と `tracks` (能力の推移の格子。各機能に世代名 `sub` を持つ)。設計文書からの要約 |
| build-site.sh | サイト組立て。tmp/build/ の成果物にサイズ / SHA-256 を付与し tmp/site/ へ集める |
| test-emu.mjs | エミュレータの検証 (node)。チェーン成果物の実行出力が QEMU とビット一致すること。OS の起動・spawn の逐次性・ゲスト内ビルド・自己再生成 (tests/stage012・013) に加え，適合台帳の probe (tests/stage014・015 の ledger.txt を読んで突き合わせる)・libc15 (lib15 / kernel16)・sfs2 の像がホストの道具とバイト一致すること・kernel17 / 18 / 19 (tests/stage016 と同じ期待値) を含む |

依存は無い (フレームワーク・ビルドツール・外部 CDN を使わない)。
エミュレータは**展示専用**であり，ビルド経路には一切使わない。
検証の基準は従来どおり QEMU + コンテナにある (docs/plan.md 2.3)。

## ローカルでの確認

```
sh tools/build.sh all          # チェーンの成果物を用意する (コンテナが要る)
node web/test-emu.mjs          # エミュレータの検証
sh web/build-site.sh           # tmp/site/ へ組み立てる
python3 -m http.server -d tmp/site 8000
```

## 配信

`.github/workflows/pages.yml` が main への push で動く:
チェーンを再ビルド (ci.yml と同じキャッシュ) → `test-emu.mjs` で
エミュレータを検証 → サイトを組み立て → `gh-pages` ブランチへ force push。

公開には一度だけリポジトリ設定が要る:
**Settings → Pages → Build and deployment → Deploy from a branch → `gh-pages` / (root)**。
以降は push のたびに自動で更新される。

## 設計判断

- **エミュレータ自作**: 成果物はフラットバイナリ + UART + test finisher という
  最小の実行モデルなので，RV32IM の解釈系 1 枚 (約 250 行) で足りる。
  外部のエミュレータを持ち込むより小さく，依存も増えない
- **正しさの根拠**: test-emu.mjs が「チェーンの実成果物を実行した出力が
  QEMU での出力とビット一致すること」を固定する (固定点 occ(occ.sc) == occ.bin，
  cc8 の自己コンパイル，OS 上の自己再生成 cc10l.bin まで走らせる)。
  pages.yml はこれを通らないと配信しない
- **PMP は受けるが強制しない**: カーネルは PMP を設定するが，エミュレータは
  CSR 書込みとして受けるだけで保護は行わない (展示ではメモリ保護の失敗を
  再現する必要がなく，U モード全許可で十分)。ecall・mret・CSR・特権遷移は
  実装している
- **世代コンテンツは静的 JSON**: 文書の要約は人が書く (自動抽出しない)。
  サイズ・SHA-256 だけを build-site.sh がビルド時に実測して付与する
- **測った数字は書き写さない**: 適合台帳 (ok / gap / bad) は
  `tests/stageNNN/ledger.txt` をビルド時に読み込んで載せる。
  probe の一覧・状態・期待値をサイト側に写すと，台帳を直したときに
  黙って古くなるからである。probe の実体が無い行があれば
  build-site.sh は失敗する (デッドリンクを出荷しない)
- **展示の手順は検査の手順に合わせる**: プレイグラウンドとターミナルの
  各段は `tests/stage*/test.sh` が sh で書いているものと同じ世代・同じ
  並びを使う。test-emu.mjs が同じ素材で期待値と突き合わせるので，
  食い違えば配信の前に落ちる
