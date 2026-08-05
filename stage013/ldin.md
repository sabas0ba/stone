# ldin 説明文書

ldin はリンカへの入力を組み立てるコマンドである。ホスト側の
`{ printf 'E'; cat a.o b.o; printf '\0'; }` のゲスト版で，設計は
[stage013-tools.md](../docs/stage013-tools.md) 7.2。

ソース [ldin.c](ldin.c) が正本である。

## ビルド

```
sh tools/build.sh stage013
# bundle(include/*.h ldin.c) | pp | cc -> ldin13.o
# ld13 'E' (ldin13.o + string + stdlib + sys + morecore + stdio) -> ldin13
```

SHA-256: 0f37ca9f97d1ccff2701789e36c8bc56342f2ddc57aa7fde82600f19862fe74b

- 形式: ELF 実行形式 (RV32, ET_EXEC)，27916 バイト

## 使い方

```
ldin E main.o string.o > main.ld
ld < main.ld > main
```

第 1 引数が出力形式 (`F` / `K` / `E`。[ld13.md](ld13.md))，以降が
並べるオブジェクトである。出力は「形式の 1 バイト + オブジェクト列 +
0」となる。

## なぜリンカと分けるのか

ld は「標準入力を読み標準出力へ書くフィルタ」であり，凍結された世代
である。その姿を変えずに OS へ移すため，入力を組み立てる側を別の
コマンドにした ([stage013-tools.md](../docs/stage013-tools.md) 7.1)。

末尾の 0 は，ld にとっては無くてもよい (オブジェクトの先頭バイトが
ELF のマジックでなくなれば止まり，OS の上では入力の終わりで read が
0 を返す)。それでも置くのは，同じファイルがホスト側の経路でもそのまま
通るようにするためである。

## 制限

- 並べられるオブジェクトは 6 本まで。kernel13 が argv を 8 個まで
  (argv[0] と形式を含む) しか渡さないためである
  ([stage013-tools.md](../docs/stage013-tools.md) 7.5)
