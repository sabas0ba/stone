# kernel 説明文書

kernel は簡易 OS のカーネルである。M モードで走り，共有領域の sfs から
ELF 実行形式を読み，U モードのユーザプロセスとして走らせる。
設計は [stage012-os.md](../docs/stage012-os.md) 5 章。

ソース [kernel.c](kernel.c) が正本である。自作の C89 で書かれており，
特権命令は一切含まない (すべて ld12 の 'K' 前置部が持つ)。

## ビルド

```
sh tools/build.sh stage012
# bundle(kernel.c) | pp | cc -> kernel.o -> ld12 'K' -> kernel.bin
```

SHA-256: 140e4557f7b9328e70a5978e76619b2752dd2fb2bfc07c9d55b043fdd8ca9db3

- 対象: RV32IM，リトルエンディアン，11152 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 実装している syscall

RV32 Linux 互換の番号で受ける ([stage012-os.md](../docs/stage012-os.md) 2.1)。

| 番号 | 名前 | 備考 |
|---|---|---|
| 56 | openat | sfs の表を引く。O_CREAT で新規作成，O_TRUNC で切詰め |
| 57 | close | |
| 63 | read | fd 0 は UART (用意が無ければ 0 = EOF)，それ以外は sfs |
| 64 | write | fd 1 / 2 は UART，それ以外は sfs |
| 93 | exit | 終了コードを test finisher へ渡す |
| 214 | brk | ユーザ領域の末尾を伸ばす |

それ以外の番号は -ENOSYS (-38)。エラーは負値 (-errno) で返す。

## 起動の流れ

1. 共有領域 (0x8400_0000) の sfs のマジックを確かめ，表を読む
2. `boot` に書かれた 1 行を起動するプログラムの名前とする
3. その ELF を読み，プログラムヘッダどおりに 0x8600_0000 へ配置し，
   `.bss` を 0 で埋める
4. ユーザスタック (0x8700_0000 の下) に argc / argv を積む
5. トラップフレームを仕立てて `urun` (前置部) へ渡す。U モードで実行が始まる

想定外のトラップ (U モードからの ecall 以外) は，mcause と mepc を
16 進で出して終了コード 9 で停止する。
