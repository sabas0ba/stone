# kernel13 説明文書

kernel13 は簡易 OS のカーネルの Stage 13 世代である。stage012 の
[kernel.c](../stage012/kernel.c) を出発点に，spawn (プログラムからの
プログラム起動)・標準入出力のつなぎ替え・fd 0 のブロッキング read を
足した。設計は [stage013-tools.md](../docs/stage013-tools.md) 3 章。

ソース [kernel.c](kernel.c) が正本である。自作の C89 で書かれており，
特権命令は一切含まない (すべて ld13 の 'K' 前置部が持つ。'K' の前置部は
ld12 と同一である)。

## ビルド

```
sh tools/build.sh stage013
# bundle(kernel.c) | pp | cc -> kernel13.o -> ld13 'K' -> kernel13.bin
```

SHA-256: 63f3217a7fe364ada3a3410a40f5b6268fe1be5893b2441c1b3bae0db3fd1f5d

- 対象: RV32IM，リトルエンディアン，19096 バイト
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 実装している syscall

RV32 Linux 互換の番号 ([stage012-os.md](../docs/stage012-os.md) 2.1) に，
独自の拡張 (500 番台) が 1 つ加わる。

| 番号 | 名前 | 備考 |
|---|---|---|
| 56 | openat | sfs の表を引く。O_CREAT で新規作成，O_TRUNC で切詰め |
| 57 | close | |
| 63 | read | fd 0 は結び先 (UART か sfs ファイル)。UART は最初の 1 バイトが届くまで待つ |
| 64 | write | fd 1 は結び先，fd 2 は常に UART |
| 93 | exit | spawn の子なら親を復元して戻る。最上位なら test finisher へ |
| 214 | brk | ユーザ領域の末尾を伸ばす |
| 500 | spawn | 起動して終わりを待つ。引数は表 {path, argv, in, out} 1 つ |

それ以外の番号は -ENOSYS (-38)。エラーは負値 (-errno) で返す。

## kernel12 からの変更

1. **spawn (500)。** 親の像 [0x8600_0000, ubrk) とフレームスタック
   [sp, 0x8700_0000)，トラップフレーム，fd 表を退避領域
   (0x8100_0000..0x8370_0000) へ複写してから子を配置し，子の exit で
   復元する。入れ子は 8 段まで。子は fd 3 以降を閉じた状態で始まる
2. **つなぎ替え。** プロセスごとに fd 0 / fd 1 の結び先 (UART か sfs の
   項目) を持つ。spawn の in / out で子の結び先を指定でき，NULL なら
   親と同じ先を継ぐ (位置も含めて親の状態は復元される)
3. **fd 0 のブロッキング read。** UART は最初の 1 バイトが届くまで待ち，
   届いている分だけ返す (kernel12 は用意が無ければ即 0 = EOF を返した)。
   入力の終わりは約束 (EOT) で表す
4. **boot 行の引数。** `boot` の 1 行を空白で区切り argc / argv として
   渡す (kernel12 は名前 1 語だけだった)

## 起動の流れ

1. 共有領域 (0x8400_0000) の sfs のマジックを確かめ，表を読む
2. `boot` に書かれた 1 行を空白で区切り，最初の語のプログラムを読む
3. ELF をプログラムヘッダどおりに 0x8600_0000 へ配置し，`.bss` を 0 で埋める
4. ユーザスタック (0x8700_0000 の下) に argc / argv を積む
5. トラップフレームを仕立てて `urun` (前置部) へ渡す。U モードで実行が始まる

想定外のトラップ (U モードからの ecall 以外) は，mcause と mepc を
16 進で出して終了コード 9 で停止する。
