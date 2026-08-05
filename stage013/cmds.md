# OS 上の処理系 (pp / cc / ld) 説明文書

pp・cc・ld のコマンド版は，**既にあるオブジェクトを ld13 の `'E'` 形式で
リンクし直したもの**である。専用のソースは無い。設計は
[stage013-tools.md](../docs/stage013-tools.md) 7.1。

3 つとも「標準入力を読み標準出力へ書くフィルタ」として書かれており，
`'E'` 前置部の getc / putc が read(0) / write(1) の 1 バイト版なので
([stage012-os.md](../docs/stage012-os.md) 5.5)，リンクし直すだけで
OS 上のコマンドになる。**元のソースは凍結されたまま，1 行も変えていない。**

## ビルド

```
sh tools/build.sh stage013
# { printf 'E'; cat <obj>; printf '\0'; } | ld13 > <cmd>
```

| コマンド | 素材 | 素材の出どころ | SHA-256 | 大きさ |
|---|---|---|---|---|
| pp | `pp.o` | [../stage009/pp.md](../stage009/pp.md) | 3ea938f8f32750a144c9ac0fc89839e50a291a3dba54f030ccb3870c618b1063 | 53564 |
| cc | `cc10l.o` | [../stage010/cc12.md](../stage010/cc12.md) | 8573e378f9b788b4c1fcb7f8e591beb290f28e7d3abd9357e8f49dd94c8b96e7 | 139584 |
| ld | `ld13.o` | [ld13.md](ld13.md) | a2cdb19955f36c2f922d2e006204b887d17a6db003be3488ceef0322b24048ab | 35356 |

- 形式: ELF 実行形式 (RV32, ET_EXEC)
- ロードアドレス: 0x8600_0000 (カーネルが配置する)

生成物の名前は `tmp/build/pp13cmd` / `cc13cmd` / `ld13cmd` である
(ベアメタル版の `pp.bin` / `cc.bin` / `ld13.bin` と区別する)。sfs へ
入れるときは `pp` / `cc` / `ld` と呼ぶ。

## 使い方

```
$ bundle util.h main.c > main.b
$ pp < main.b > main.i
$ cc < main.i > main.o
$ ldin E main.o > main.ld
$ ld < main.ld > main
$ main
```

入力の組み立て (`bundle` / `ldin`) は [bundle.md](bundle.md) /
[ldin.md](ldin.md) を参照。

## 入力の終わり

- **pp と cc は EOT (0x04) でしか読取りを止めない。** OS の上では入力の
  終わりで read が 0 を返すが，どちらも `while (c != eot)` で読むので
  EOT が無いと止まらない。`bundle` が末尾へ EOT を置き，pp の出力も
  末尾が EOT なので，上の手順では手当てが要らない
- ld はオブジェクトの先頭バイトが ELF のマジック (127) でなくなった
  ところで止まる。`ldin` が置く末尾の 0 でも，入力の終わり (read が 0)
  でも止まる

## 同一性

各段の生成物が **ホスト経路の同じ段の生成物とバイト一致する** ことを
[../tests/stage013/test.sh](../tests/stage013/test.sh) が確認する。
移したのが同じ処理系であることの機械的な証明である。
