# web: bootstrap の系譜 (GitHub Pages)

チェーンの各世代を「仕様の要約・成果物・プレイグラウンド」の 3 点で見せる
静的 Web アプリ。世代スライダーで Stage 1〜14 を行き来し，進化を追える。

成果物はすべて本物のチェーン生成物 (tools/build.sh の出力) であり，
ブラウザ内の RV32IM エミュレータ (rv32.js) で「UART フィルタ」として
その場で実行できる。hex を打てば hex0 が変換し，C を書けば
ブートストラップされた本物の cc がコンパイルする。

## 構成

| ファイル | 内容 |
|---|---|
| rv32.js | RV32IM エミュレータ (UART + test finisher。docs/plan.md 3 章の実行モデル) |
| worker.js | プレイグラウンドの実行係 (Web Worker)。パイプラインを順に実行する |
| pipelines.js | 各世代のパイプライン定義 (tools/build.sh・tests/ と同じ手順の再現) |
| app.js / index.html / style.css | UI 本体 (世代スライダー・要約・成果物・チャート) |
| data/stages-content.json | 世代コンテンツ (仕様要約・新機能・成果物説明)。設計文書からの要約 |
| build-site.sh | サイト組立て。tmp/build/ の成果物にサイズ / SHA-256 を付与し tmp/site/ へ集める |
| test-emu.mjs | エミュレータの検証 (node)。チェーン成果物の実行出力が QEMU とビット一致すること |

依存は無い (フレームワーク・ビルドツール・外部 CDN を使わない)。
エミュレータは**展示専用**であり，ビルド経路には一切使わない。
検証の基準は従来どおり QEMU + コンテナにある (docs/plan.md 2.3)。

## ローカルでの確認

```
sh tools/build.sh all          # チェーンの成果物を用意する (コンテナが要る)
node web/test-emu.mjs          # エミュレータの検証 (13 検査)
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
  QEMU での出力とビット一致すること」を固定する (固定点 occ(occ.sc) == occ.bin や
  cc8 の自己コンパイルまで走らせる)。pages.yml はこれを通らないと配信しない
- **Stage 12 / 13 はプレイグラウンド対象外**: 成果物が OS (カーネル + ELF 実行
  形式 + sfs) でありフィルタ型ではない。対応するにはエミュレータに M/U モード・
  CSR・PMP の実装が要る。将来の拡張とする
- **世代コンテンツは静的 JSON**: 文書の要約は人が書く (自動抽出しない)。
  サイズ・SHA-256 だけを build-site.sh がビルド時に実測して付与する
