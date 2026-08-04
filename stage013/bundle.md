# bundle 説明文書

bundle はソース群を pp への入力 (束ね) へ変換するコマンドである。
ホスト側の [../tools/bundle.sh](../tools/bundle.sh) のゲスト版で，
形式は [stage009-pp.md](../docs/stage009-pp.md) 2.2。設計は
[stage013-tools.md](../docs/stage013-tools.md) 7.2。

ソース [bundle.c](bundle.c) が正本である。自作の C89 + 第 13 世代の
libc で書かれている。

## ビルド

```
sh tools/build.sh stage013
# bundle(include/*.h bundle.c) | pp | cc -> bundle13.o
# ld13 'E' (bundle13.o + string + stdlib + sys + morecore + stdio) -> bundle13
```

SHA-256: a30785ae7fc224aeb1af8308eff258f08689739113eacc6896187a21dc6dd25e

- 形式: ELF 実行形式 (RV32, ET_EXEC)，28372 バイト

## 使い方

```
bundle util.h main.c > main.b
```

最後に並べたファイルが前処理対象の翻訳単位になる。それより前のものは
`#include` で参照できる (依存を先に，本体を後に並べる)。

出力は次の形式である。

```
bundle := "#!stone-bundle\n" member* EOT
member := "@" name " " size "\n" content
```

- 名前は与えられた引数をそのまま使う。sfs の名前空間はフラットなので，
  ホスト版の `basename` にあたる処理は要らない
- 大きさを本文より先に出す形式なので，各ファイルを 2 度読む
  (全体を溜める記憶域を要らなくするための割り切り)
- 読めないファイルがあれば標準エラーへ出して終了コード 1

## 末尾の EOT

**末尾に EOT (0x04) を置くのがこのコマンドの要である。** OS の上では
入力の終わりで read が 0 を返すが，pp と cc は EOT でしか読取りを
止めない。どちらも凍結された世代で手を入れられないので，EOT を置くのは
束ねを作る側の役目になる ([cmds.md](cmds.md))。

## 制限

- 並べられるファイルは 7 本まで。kernel13 が argv を 8 個まで (argv[0] を
  含む) しか渡さないためである
  ([stage013-tools.md](../docs/stage013-tools.md) 7.5)
