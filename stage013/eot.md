# eot 説明文書

eot は標準入力を標準出力へ写し，末尾へ EOT (0x04) を置くフィルタである。
ホスト側の `{ cat src; printf '\004'; }` のゲスト版で，設計は
[stage013-tools.md](../docs/stage013-tools.md) 7.2。

ソース [eot.c](eot.c) が正本である。

## ビルド

```
sh tools/build.sh stage013
# bundle(include/*.h eot.c) | pp | cc -> eot13.o
# ld13 'E' (eot13.o + string + stdlib + sys + morecore + stdio) -> eot13
```

SHA-256: fc587eeebfd7107a53c28bca4809566af16ad476b6a3c0991c6f51cea1fa9079

- 形式: ELF 実行形式 (RV32, ET_EXEC)，26800 バイト

## 使い方

```
eot < cc12.sc > cc12.in
cc < cc12.in > cc12.o
```

## なぜ要るのか

pp と cc は EOT でしか読取りを止めない (どちらも凍結された世代で手を
入れられない)。束ねを作る場合は [bundle](bundle.md) が末尾へ EOT を
置くが，`.sc` のように **束ねを通さず直接 cc へ渡すソース** には
この道具が要る。

処理系自身のソース (`stage010/cc12.sc`・`stage009/pp.sc`・
`stage013/ld13.sc`) はどれも指令を含まないので前処理を通さない。
ゲスト内で処理系を作り直す経路はこの道具から始まる
([stage013-tools.md](../docs/stage013-tools.md) 7.3)。

中身は見ないので，任意のバイト列をそのまま通す。
