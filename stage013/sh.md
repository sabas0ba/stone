# sh 説明文書

sh は行指向の最小シェルである。OS (kernel13) の上で動く ELF 実行形式の
ユーザプログラムで，コマンド行を読み，sfs 内のプログラムを spawn で
起動する。設計は [stage013-tools.md](../docs/stage013-tools.md) 3.6。

ソース [sh.c](sh.c) が正本である。自作の C89 + 第 13 世代の libc
([libc/](libc/)) で書かれている。バイナリはビルドで再現される生成物で
あり，git 管理しない。

## ビルド

```
sh tools/build.sh stage013
# bundle(include/*.h sh.c) | pp | cc -> sh13.o
# ld13 'E' (sh13.o + string + stdlib + sys + morecore + stdio) -> sh13
```

SHA-256: ca43edda8063856482dd8d5701b42fc15f2dfea032546e5c7df46a459ffec654

- 形式: ELF 実行形式 (RV32, ET_EXEC)，28796 バイト
- ロードアドレス: 0x8600_0000 (カーネルが配置する)

## 使い方

```
$ コマンド 引数 ...            sfs 内のファイル名を spawn する
$ コマンド < 入力 > 出力       子の標準入出力を sfs ファイルへ結ぶ
$ echo 語 ...                  語を空白 1 つで繋いで出す (組込み)
$ exit [n]                     シェルを終える (省略時 0)
```

- 区切りは SP / TAB。`<` / `>` は独立した語として書く (前後に空白)
- 子の終了コードが 0 以外なら `? N` を出す。起動に失敗したら
  `sh: <名前>: errno <E>` を出して次の行へ進む
- 入力の終わり (EOT または結び先ファイルの EOF) で終了コード 0
- パイプ・変数・引用符・グロブは持たない
