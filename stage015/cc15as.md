# cc15as --- C コンパイラ 第 15 世代 その 45

`cc15ar` との差は，`?:` の結果の型を通常の算術変換の型にすることである (註を除く)。ソースは `stage015/cc15as.sc`。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.15。

stone の OS の上の cc1 と host の cc1 の `.s` を突き合わせ，浮動小数点の定数が違うことから辿った。

## 直したもの: `?:` の結果の型

2 つの腕がどちらも算術型なら，結果の型は通常の算術変換の型である (C89 6.3.15)。`cc15ar` までは 1 語の整数どうしでは then 側の型を採っていたので，`c ? 0 : u` (u は unsigned) が int になった。後に続く右シフトは算術シフトに，割り算と大小比較は符号つきになる。

GCC の real.c の `lshift_significand` は

```c
r->sig[SIGSZ-1-i]
  = (((ofs + i >= SIGSZ ? 0 : a->sig[SIGSZ-1-i-ofs]) << n)
     | ((ofs + i + 1 >= SIGSZ ? 0 : a->sig[SIGSZ-1-i-ofs-1])
        >> (HOST_BITS_PER_LONG - n)));
```

と書く。最上位が 1 の語を算術シフトすると上位が 1 で埋まり，仮数が壊れる。stone の OS の上の cc1 は `3.14159265358979` を `0x400fffff ffc42d11` (正しくは `0x400921fb 54442d11`) にしていた。

調べた順:

1. cc1 の`.s`の浮動小数点の定数が host の cc1 と違う (`1e-4` の下位語など)
2. 同じ値を GMP / MPFR で作るプログラムを stone の OS で走らせ，host と一致することを確かめた (53 bit と GCC の 160 bit。`mpfr_strtofr`・`mpfr_get_str` も)
3. real.c の `real_from_string` と `real_to_target` を直に呼ぶプログラムを cc1 の部品と組んで走らせ，16 進の文字列からの変換で壊れることを確かめた
4. 16 進の読込みのループを probe にして host と一致することを確かめ，その後の正規化 (`normalize` → `lshift_significand`) に絞った

1 語の整数どうしは，どちらかが unsigned int なら unsigned int，そうでなければ int にする (char / short は int に格上げされる)。2 語の値 (64 bit 整数・double) と，腕の片方が浮動小数点の場合は従来の経路のままである。

## ビルドチェーンは変わらない

`sh` / `ed` / `mk` を訳した `.o` は `cc10l` のものと 1 バイトも変わらない。

## 測り方

`tests/stage015/probe/condty.c` は `c ? u : 0` と `c ? 0 : u` の右シフト・割り算・大小比較，`-1` と unsigned の組，unsigned short (int に格上げ)，int と double の組を見る。

| | `cc15ar` | `cc15as` |
|---|---|---|
| `tools/diff17.sh condty` | **値が違う** (else 側が unsigned のとき) | 値が一致 |

## ビルド

```
sh tools/build.sh stage015
# cc15ar(cc15as.sc) -> cc15as0    (1 段目)
# cc15as0(cc15as.sc) -> cc15as    (2 段目。以降は固定点)
```

SHA-256: fa1eff5a8be779db7057930834764bab2e1410e70dfb070ffa991ed3be710906

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
