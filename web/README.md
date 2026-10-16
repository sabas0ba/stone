# web: ブートストラップの各段階を確認する GitHub Pages

各 Stage の仕様、成果物、実行例を表示する静的 Web アプリ。
スライダーで Stage 1〜19 を選択し、実装された機能と成果物を確認できる。
実行に使うバイナリは `tools/build.sh` の生成物であり、
ブラウザ内の RV32IM エミュレータ (`rv32.js`) で動作する。
入力した hex テキストは hex0 が変換し、C ソースは各世代のコンパイラが処理する。

## 機能一覧と画面操作

「能力の推移」(Capability growth) は、10 系統 × 19 世代の表で追加機能を示す。
コンパイラの機能と実行環境・OS の機能を色分けし、成果物のサイズも同じ表に表示する。

- セルを押すと、その世代・系統の追加機能を表の下に表示する。もう一度押すと閉じる。
- 行名を押すと、kernel12〜kernel29、cc15a〜cc15as、ld〜ld18 などの内訳を表示する。

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

Stage 12 / 13 / 15 / 16 / 17 / 18 では、kernel と sfs イメージを起動して対話操作できる。

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
- **Stage 17**: 9 つの実行シナリオで、OS の上のツール群を世代順に確認する。
  `sh2` は標準入力を EOF まで読んでから実行するため、対話ではなく
  **台本 (`go.sh`) を編集して `sh2 go.sh` で起動する** (`tests/stage017/test.sh` と同じ手順)。
  Guided tour には検査の各節の台本を置き、期待値は `tests/stage017/expected` にある。

  | シナリオ | kernel | sfs / 配置 | RAM | 内容 |
  |---|---|---|---|---|
  | cc | kernel23 | sfs2 @64 MiB | 512 MiB | `cc -o hello hello.c`、selfhost、argv 64 語と E2BIG |
  | ar | kernel23 | sfs2 | 512 MiB | `cc18 -c` / `ar rcs` / 書庫を使った結合 |
  | mk | kernel23 | sfs2 | 512 MiB | mk17 が libc.mk から libc を組む。mk19 の関数 (fn.mk) |
  | sfs3 time | kernel24 | sfs3 @64 MiB | 512 MiB | `stamp` が表の時刻を読む。mk18 の差分ビルド |
  | sfs4 names | kernel25 | sfs4 @512 MiB | 1 GiB | 92 バイトの名前と深さ 12 の経路 |
  | times | kernel27 | sfs4 @512 MiB | 1 GiB | `tmx`: times / sysconf / pathconf と 255 バイトの経路 |
  | libc26 + ld18 | kernel28 | sfs4 @512 MiB | 1 GiB | `l26x.c` をブラウザ内で pp16 → cc15as → ld18 で組み、走らせる |
  | 2 GiB | kernel29 | sfs4 @1.5 GiB | 2 GiB | `bigheap` が 768 MiB を取る |
  | 256 MiB | kernel28 | sfs4 @512 MiB | 1 GiB | 同じ `bigheap` が 256 MiB の内で止まる (比べる相手) |

  kernel24 以降は goldfish RTC を読むので、エミュレータはブラウザの時計を
  epoch からのナノ秒として返す。実行後のツリーでは項目の時刻を title に表示する。
  `times` の `own-small` は、エミュレータが QEMU より 1 桁遅いため n になりうる
  (子の時間は 3 tick しか無く、spawn の経路だけでそれを超える)。
- **Stage 18**: kernel29 の上で `nas1` に `insn.s` / `elf.s` を組み立てさせ、範囲外の
  分岐 (`far.s`) を拒むことを見る。実行後のツリーでは `.o` の節見出し
  (名前・種類・旗・大きさ・整列) を `readelf -S` の要約として表示する。
  `tests/stage018/nas1/elf.expect` と見比べられる。
- **Stage 19**: 対話デモは無い。Linux 5.4 のソース (約 1.2 GB) は repo に入れない
  決まり (docs/SOURCES.md) であり、成果は設計文書の測定結果である。

## 構成

| ファイル | 内容 |
|---|---|
| rv32.js | RV32IM エミュレータ。UART + test finisher (docs/plan.md 3 章) に加え，M/U 特権・CSR・mret・ecall トラップ (ld12/ld13 の 'K' 前置部と kernel が使う範囲。docs/stage012-os.md 5 章)。入力が尽きると停止し，足すと続きから走る (対話実行)。goldfish RTC (0x0010_1000。kernel24 以降が時刻と times に使う)。RAM の大きさは指定できる (既定 128 MiB / kernel19〜24 は 512 MiB / kernel25〜28 は 1 GiB / kernel29 は 2 GiB)。大きな RAM は WebAssembly.Memory で予約し，触れたページだけを確保する |
| sfs.js | sfs イメージの構築・読出し (tools/sfs.sh の JS 版。docs/stage012-os.md 4 章) |
| sfs2.js | sfs2 (ディレクトリを持つ) の構築・読出し (tools/sfs2.sh の JS 版。docs/stage016-os.md 6.3)。項目の並びまで同じにしてあり，同じツリーからは同じイメージが出る |
| sfs3.js / sfs4.js | sfs3 (項目が更新時刻を持つ。docs/stage017-cc.md 11.3) と sfs4 (名前の枠が 103 バイト。docs/stage017-gcc.md 7.1) の構築・読出し。実装は 1 つ (`makeSfs`) で，形式の差は magic・項目の幅・名前の枠だけ。時刻は BigInt のナノ秒で受け渡す |
| ld18.js | ld18 の駆動 (tools/ld18.sh の JS 版)。入力を RAM の 0x9000_0000 に置き，0xa800_0000 から実行形式を取り出す。ライブラリの部品 (-L) と必ず組むもの (-N) の並びも同じ |
| worker.js | 実行処理 (Web Worker)。パイプライン (ld18 の段を含む) と OS セッション (ターミナル) の両方。sfs の世代と配置はシナリオごとに受ける |
| pipelines.js | 各世代のパイプライン定義とターミナル定義 (tools/build.sh・tests/ と同じ手順の再現)。Stage 17 / 18 は台本 (`go.sh`) を `sh2 go.sh` で走らせる mode: 'script' |
| app.js / index.html / style.css | UI 本体 (世代スライダー・能力の推移の格子 + 成果物の大きさ・要約・成果物・プレイグラウンド・ターミナル・台本の編集・.o の節見出し) |
| data/stages-content.json | 世代コンテンツ (仕様要約・新機能・成果物説明) と `tracks` (能力の推移の格子。各機能に世代名 `sub` を持つ)。設計文書からの要約 |
| build-site.sh | サイト組立て。tmp/build/ の成果物にサイズ / SHA-256 を付与し tmp/site/ へ集める |
| test-emu.mjs | エミュレータの検証 (node)。チェーン成果物の実行出力が QEMU とビット一致すること。OS の起動・spawn の逐次性・ゲスト内ビルド・自己再生成 (tests/stage012・013) に加え，適合台帳の probe (tests/stage014・015 の ledger.txt を読んで突き合わせる)・libc15 (lib15 / kernel16)・sfs2 / sfs3 / sfs4 のイメージがホストのツールとバイト一致すること・kernel17 / 18 / 19 (tests/stage016 と同じ期待値)・kernel23〜29 の上の cc / ar / mk / stamp / tmx / l26x / bigheap (tests/stage017 と同じ期待値)・ld18 の JS 駆動がチェーンの nas1 を再現すること・nas1 (tests/stage018。elf.expect の節見出し，ホストの cc で組んだ nas1 との .o の一致) を含む |

依存は無い (フレームワーク・ビルドツール・外部 CDN を使わない)。
エミュレータは**デモ専用**であり，ビルド経路には一切使わない。
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
  最小の実行モデルなので，RV32IM のインタプリタ 1 ファイル (約 250 行) で足りる。
  外部のエミュレータを持ち込むより小さく，依存も増えない
- **正しさの根拠**: test-emu.mjs が「チェーンの実成果物を実行した出力が
  QEMU での出力とビット一致すること」を固定する (固定点 occ(occ.sc) == occ.bin，
  cc8 の自己コンパイル，OS 上の自己再生成 cc10l.bin まで走らせる)。
  pages.yml はこれを通らないと配信しない
- **PMP は受けるが強制しない**: カーネルは PMP を設定するが，エミュレータは
  CSR 書込みとして受けるだけで保護は行わない (デモではメモリ保護の失敗を
  再現する必要がなく，U モード全許可で十分)。ecall・mret・CSR・特権遷移は
  実装している
- **RAM は予約して触れた分だけ使う**: kernel29 は RAM を 2 GiB (0x8000_0000〜
  0xffff_ffff) として sfs の窓を 0xe000_0000 に置く。2 GiB を実際に確保すると
  多くのブラウザで失敗するので，WebAssembly.Memory で予約し，触れたページだけを
  OS に確保させる。`bigheap` が 768 MiB を書く場面では本当にその分を使う
- **時計は本物**: kernel24 以降の goldfish RTC にはブラウザの時計を返す。
  差分ビルド (mk18) は「OS が書いたファイルに今の時刻が立つ」ことに依るので，
  固定の時刻では再現できない。ホストが詰めた時刻は sfs3 / sfs4 の表にそのまま入る
- **世代コンテンツは静的 JSON**: 文書の要約は人が書く (自動抽出しない)。
  サイズ・SHA-256 だけを build-site.sh がビルド時に実測して付与する
- **測った数字は書き写さない**: 適合台帳 (ok / gap / bad) は
  `tests/stageNNN/ledger.txt` をビルド時に読み込んで表示する。
  probe の一覧・状態・期待値をサイト側に複製すると，台帳を修正したときに
  通知なしに古くなるからである。probe の実体が無い行があれば
  build-site.sh は失敗する (デッドリンクを出荷しない)
- **デモの手順は検査の手順に合わせる**: プレイグラウンドとターミナルの
  各段は `tests/stage*/test.sh` が sh で書いているものと同じ世代・同じ
  並びを使う。Stage 17 / 18 の台本は検査の節と同じ `go.sh` である。
  test-emu.mjs が同じ素材で期待値と突き合わせるので，食い違えば配信の前に落ちる
