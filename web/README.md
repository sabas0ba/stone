# web: bootstrap の系譜 (GitHub Pages)

チェーンの各世代を「仕様の要約・成果物・プレイグラウンド」の 3 点で見せる
静的 Web アプリ。世代スライダーで Stage 1〜16 を行き来し，進化を追える。

成果物はすべて本物のチェーン生成物 (tools/build.sh の出力) であり，
ブラウザ内の RV32IM エミュレータ (rv32.js) で「UART フィルタ」として
その場で実行できる。hex を打てば hex0 が変換し，C を書けば
ブートストラップされた本物の cc がコンパイルする。

**能力の推移** (Capability growth) は 10 系統 × 16 世代の格子で，
どの世代がどの系統に何を足したかを 1 枚で見せる。Stage 14 以降は
足し込みの本数が多く，世代ごとのチップだけでは「積み上がっている」
ことが読めないために置いた。押した升の系統は全世代ぶんの一覧になる。
コンパイラ側 (琥珀) と実行環境側 (菫) を塗り分けてあるので，
Stage 15 から 16 で律速が移ったことがそのまま見える。

**適合台帳を持つ世代 (14 / 15) はプレイグラウンドで probe を選べる。**
選択肢は `tests/stageNNN/ledger.txt` から作るので，台帳を直せば
サイトの選択肢も期待値も追随する。選んだ probe の状態 (ok / gap / bad) と
期待値をその場に出し，実際に走らせた結果と見比べられる。`gap` は
「cc がわざと拒む」ものなので，本当に非ゼロで終わるところまで見える。

Stage 12 / 13 / 15 / 16 には**ターミナル**があり，自作 OS そのもの
(kernel + sfs) をエミュレータで起動して対話できる。

- **Stage 12 / 13**: シェル・ed・cc / ld / pp が OS の上で動く。案内
  (guided tour) に従って Enter を押すだけで「エディタで書く → ゲスト内で
  コンパイル → 実行 → **cc が自分自身を作り直す**」まで体験できる。
  セッション終了後は sfs 内のファイル (自分で作った cc10l.bin を含む) を
  ダウンロードできる
- **Stage 15**: `lib15` を kernel16 の上で走らせ，`%llu` / `%f` /
  `snprintf` / `strtod` / `sscanf` / `setjmp` / `lseek` が効くところを見せる
  (プレイグラウンドの probe が bit 列を出すのは，ベアメタルに printf が
  無いからである。その対比になっている)
- **Stage 16**: 筋書きを 4 つ置き，kernel17 (経路解決)・kernel18
  (ディレクトリ操作)・kernel19 (256 MB) を切り替えられる。**記憶域は
  kernel18 と kernel19 の両方を置いてある** —— 片方だけを見ても
  「広がった」ことは言えないため (docs/stage016-os.md 8.6)。
  走らせたあとは sfs2 の**木**が出るので，`dirprobe` が掘った `out/` が
  増えているのが見える。kernel19 の筋書きだけは 512 MB の
  エミュレータを確保する (配置が 0xa000_0000 まで届くため)

## 構成

| ファイル | 内容 |
|---|---|
| rv32.js | RV32IM エミュレータ。UART + test finisher (docs/plan.md 3 章) に加え，M/U 特権・CSR・mret・ecall トラップ (ld12/ld13 の 'K' 前置部と kernel が使う範囲。docs/stage012-os.md 5 章)。入力が尽きると停止し，足すと続きから走る (対話実行)。RAM の大きさは指定できる (既定 128 MiB / kernel19 は 512 MiB) |
| sfs.js | sfs イメージの構築・読出し (tools/sfs.sh の JS 版。docs/stage012-os.md 4 章) |
| sfs2.js | sfs2 (ディレクトリを持つ) の構築・読出し (tools/sfs2.sh の JS 版。docs/stage016-os.md 6.3)。項目の並びまで同じにしてあり，同じ木からは同じ像が出る |
| worker.js | 実行係 (Web Worker)。パイプラインと OS セッション (ターミナル) の両方 |
| pipelines.js | 各世代のパイプライン定義とターミナル定義 (tools/build.sh・tests/ と同じ手順の再現) |
| app.js / index.html / style.css | UI 本体 (世代スライダー・要約・能力の推移・成果物・チャート・プレイグラウンド・ターミナル) |
| data/stages-content.json | 世代コンテンツ (仕様要約・新機能・成果物説明) と `tracks` (能力の推移の格子)。設計文書からの要約 |
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
