# cc15k 説明文書

cc15k は C コンパイラの Stage 15 第 4 部の最初の世代である。
[cc15j.sc](cc15j.sc) を出発点に，**可変部の 2 語の値** (long long と
double) を入れた。`printf("%llu", x)` が tcc の数値文字列化で実行時に
走るため，第 4 部 (libc) の前提になる
([stage015-tcc.md](../docs/stage015-tcc.md) 11.3)。

ソース [cc15k.sc](cc15k.sc) が正本である。

## ビルド

```
sh tools/build.sh stage015
# cc15j(cc15k.sc) -> cc15k0     (1 段目)
# cc15k0(cc15k.sc) -> cc15k     (正本。以降は固定点)
```

SHA-256: b4f3c83c731e790bc01e16e09304b2ead990930e6edf48a1dae59e2e1b11682a

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 形

可変部は**逆順に積む** (積んだ順の逆がメモリの昇順になる) ので，
2 語の値は**上位語を先に**積めば下位語が低い番地に来る。読む側
(va_arg) は型の語数ぶん進めるだけでよい (stage015 の
[stdarg.h](libc/include/stdarg.h))。

- float は C の既定の実引数拡張で double へ格上げして積む
- 構造体は従来どおり可変部に置けない (拒む)
- 変更は ecallseq の可変部ループ 1 箇所だけである

## 検証

- 固定点: cc15k(cc15k.sc) == cc15k0(cc15k.sc) (B2 == B3)
- 回帰: sh / ed / mk と rt64.c / rtfp.c の `.o` が変わらない
- 台帳: llvarg (long long・float の格上げ・後続の int・名前つき引数の
  並び) ok
