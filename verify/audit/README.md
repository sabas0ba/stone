# 独立監査 (verify/audit)

本書は，ブートストラップ鎖が適切に成立していることを **鎖自身に依存せずに**
検証する手順を定める。2026-08 に実施した全段監査で用いた手順を，再実行可能な
形に整理したものである。

[plan.md](../../docs/plan.md) 2.2 の verify 層と同じ読取り専用の位置づけであり，
ビルド成果物には一切影響しない。ここに置くものを **ビルド経路に使ってはならない**。

## 1. 何が問題か (信頼モデル)

鎖の検証体系は次の三重になっている。

1. 手エンコードゴールデンとの一致 (tests/stage00N)
2. セルフホストの固定点 (B2 == B3 のバイト一致。テスト実行のたびに再導出)
3. `.md` 記載の SHA-256 との照合 (回帰ピン。CI が毎回照合し，週次でゼロから再現)

このうち 3 の記録値は **初回ビルドの自己記録** (trust-on-first-use) であり，
1 と 2 も実行主体は鎖の成果物そのものである。したがって「seed (hex0.bin) と
listing が揃って間違っている」「ゴールデンと被検体が同じ誤解を共有している」
という形の誤りは，鎖の中だけでは原理的に検出できない。

本監査はこれを **仕様書だけから独立に書き起こした参照実装** で外側から
突き合わせることで補う。鎖 (RV32 バイナリ, QEMU 上) と参照実装 (Python,
ホスト上) は実装・実行環境・作者経路のすべてが異なるため，両者の出力が
ビット一致すれば「揃って間違う」余地は仕様の誤読が両者で偶然一致する場合に
限られる。

## 2. 構成

| ファイル | 内容 |
|---|---|
| hex0.py | hex0 言語の参照実装 ([stage001-hex0.md](../../docs/stage001-hex0.md) 2 章) |
| hex1.py | hex1 言語の参照実装 ([stage002-hex1.md](../../docs/stage002-hex1.md) 2 章) |
| asm.py | asm 言語 (RV32IM) の参照実装 ([stage003-asm.md](../../docs/stage003-asm.md) 2 章) |
| audit.sh | 検査の駆動。ホスト側 python3 のみで動き，QEMU・コンテナを要さない |

エラーコードも各仕様と同一に実装してあるが，監査の対象は正常系の出力一致で
ある (エラー系は tests/stage00N が実機で検査する)。

## 3. 検査の一覧

```
bash verify/audit/audit.sh
```

| # | 検査 | 意味 |
|---|---|---|
| 1 | sha256(hex0.bin) == hex0.md 記載値 | seed の完全性 (テストと同じ照合を鎖の外で再確認) |
| 2 | ref_hex0(hex0.hex) == hex0.bin | **信頼根の独立再導出**。listing (hex0.hex) とバイナリの整合を，hex0.bin 自身を実行せずに確認する。実機の自己再生成テストが持つ循環 (bin と listing が揃って誤っていても通る) を外から断つ |
| 3 | ref_hex0(hello.hex) == hello.bin | Stage 0 ゴールデンの独立再導出 |
| 4 | sha256(ref_hex0(hex1.hex)) == hex1.md 記載値 | 記録 SHA の独立再導出 (hex1.bin は hex0.bin を使わず再現できる) |
| 5 | ref_hex1(hex0.hex) == hex0.bin | hex1 の上位互換性の独立確認 |
| 6 | ref_hex1(labels.hex1) == ref_hex0(labels-expected.hex) | ラベル機能ゴールデンの独立再導出 (被検体とゴールデンの両方を参照実装で再現) |
| 7 | sha256(ref_hex1(asm.hex1)) == asm.md 記載値 | 記録 SHA の独立再導出 |
| 8 | ref_asm(rv32im.s) == ref_hex0(rv32im-expected.hex) | RV32IM 全 48 命令 + 全疑似命令 + ディレクティブのエンコードを独立実装で再現 |
| 9 | ref_asm(asm.hex1 の #: 転記) == ref_hex1(asm.hex1) | 自己アセンブル固定点を参照実装のみで再現 (手エンコードと転記コメントの相互検証を鎖の外で閉じる) |
| 10-11 | 参照実装の出力 == tmp/build の生成物 | 鎖が実際に作った hex1.bin / asm.bin との突き合わせ (ビルド済みの場合のみ) |

## 4. 監査の範囲と限界

- 独立再導出が届くのは **Stage 1〜3 (信頼根とアセンブラまで)** である。
  Stage 4 以降を参照実装で覆うのは実装量が釣り合わない。そこから先の正しさは
  鎖の三重検証 (ゴールデン・固定点・SHA ピン) と，Stage 6 の B1 == B2
  (sol 実装の sc と sc 実装の scc という **別実装 2 系統の出力一致**) が担う。
  信頼根さえ独立に立てば，以降の各段は「前段の成果物のみでビルドされる」
  ことがビルド構成 (tools/build.sh) で機械的に決まっている。
- 参照実装は仕様書から書いたものであり，仕様書自体の誤り (実装と仕様が同じ
  意図で揃って誤っている場合) は検出できない。これは listing 注釈との照合
  (verify/disasm.sh, 現状は手動) が補う領域である。

## 5. 全段監査の手順 (2026-08 実施の再現)

Stage 1〜3 の独立監査 (上記) に加えて，以下で鎖全体を検証した。

### 5.1 実機での全段再ビルドとテスト

```
sh tools/env.sh build        # コンテナ像の構築と packages.lock の照合
bash tools/test.sh           # 全 Stage の生成 + SHA 照合 + 固定点 + 機能テスト
```

コンテナが使えない環境の注意 (dockerd の起動,
snapshot.debian.org への egress) は [dev-notes.md](../../docs/dev-notes.md)
1.2 を参照。全体で 10 分を超えるためバックグラウンド実行が安全である
(同 4 章)。判定は末尾の `result: all passed` と各 Stage の
`passed: N, failed: 0`。

### 5.2 実機テストが検証している主要な性質 (読みどころ)

| 性質 | 場所 |
|---|---|
| seed の自己再生成・交差検証 | tests/stage001/test.sh |
| 固定点 B2 == B3 (テスト時に再導出して cmp) | tests/stage006, 007, 008, 010 (cc10a〜cc10l の全世代), 014 |
| 別実装 2 系統の一致 B1 == B2 | tests/stage006/test.sh |
| 'F' 出力の世代間一致 (ld12/ld13 が ld と同じバイト列を出す) | tests/stage012, 013 |
| ゲスト内再生成 == 鎖の成果物 (OS 上で cc 自身を作り直す) | tests/stage013/test.sh |
| 適合台帳と実測の照合 (通る/拒む/誤りの三値) | tests/stage014/test.sh |

### 5.3 凍結・純度の確認

```
# 凍結: 各世代のソースが成果物記録後に変更されていないこと
git log --oneline -- stage011/libc stage012/libc stage013/libc

# libc 世代間の差分が意図した範囲 (morecore 分離, spawn 追加) に限られること
diff -r stage011/libc stage012/libc
diff -r stage012/libc stage013/libc

# ポリシー強制: コンテナに as / ld が存在しないこと
sh tools/env.sh run sh -c 'command -v riscv64-unknown-elf-as; command -v riscv64-unknown-elf-ld; true'
```

ビルド経路がコンテナ内 QEMU と cat/printf のみで構成されることは
tools/build.sh の読解で確認する (ホスト側ツールチェーンの混入がないこと)。
