# 開発環境 (コンテナ)

環境操作はすべて `tools/env.sh` を介して実行する (ローカル・CI 共用)。
コンテナエンジンは podman / docker のどちらでもよく，`STONE_ENGINE` で明示指定できる
(未指定時は podman, docker の順に自動検出)。

## イメージのビルドとバージョン固定

```
sh tools/env.sh build
```

バージョン固定は次の 2 層で行う:

1. ベースイメージ: digest 固定 (env/Containerfile)
2. apt パッケージ: snapshot.debian.org のアーカイブ時点指定。アーカイブは不変であり，
   全依存パッケージのバージョンは時点から一意に定まる

`env/packages.lock` は導入されるべき全パッケージのバージョンを列挙した検証用ファイルであり，
`build` はビルド後に導入結果と照合し，不一致の場合は失敗する。
アーカイブ時点または導入パッケージを意図的に変更した場合のみ，以下で再生成し commit する:

```
sh tools/env.sh lock
```

## QEMU の実行

各 Stage の処理系は「stdin → UART RX，UART TX → stdout」のフィルタとして動作する。

```
sh tools/env.sh qemu <stageN.bin> < input > output
```

任意コマンドの実行 (repo は /work にマウントされる):

```
sh tools/env.sh run <cmd...>
```

## 観測 (デバッグ・トレース)

いずれも観測専用であり，成果物の出力には影響しない (tools/run-qemu.sh 参照)。

実行トレースの記録 (実行命令と割込み・例外を命令単位で記録):

```
STONE_QEMU_TRACE=tmp/trace.log sh tools/env.sh qemu <stageN.bin> < input > output
```

GDB stub によるステップ実行 (最初の命令の実行前に停止し，接続を待つ。
ポートはホストの 127.0.0.1 のみに公開される):

```
STONE_QEMU_GDB=1234 sh tools/env.sh qemu <stageN.bin>
# 別端末から: gdb-multiarch -ex 'set arch riscv:rv32' -ex 'target remote 127.0.0.1:1234'
```

GDB クライアント (gdb-multiarch) はイメージに含まれる。コンテナ内で完結させる場合:

```
sh tools/env.sh run bash
# コンテナ内:
STONE_QEMU_GDB=1234 sh tools/run-qemu.sh <stageN.bin> < input > output &
gdb-multiarch -ex 'set arch riscv:rv32' -ex 'target remote :1234'
```

## リグレッションテスト

実行基盤全体の検証 (ビルド・lock 照合・QEMU 実行・トレース・GDB stub):

```
bash tools/test.sh
```

## ポリシー

- as / ld はイメージから削除済み (docs/plan.md 2.1, 2.2)。
- binutils は verify 層での逆アセンブル照合 (objdump) のみに使用する。
