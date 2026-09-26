# cc15aj --- C コンパイラ 第 15 世代 その 36

`cc15ai` との差は typedef 名の扱いの 3 か所 (`typedef1()`・`tdfind()`・`istype()`) である (註を除く)。ソースは `stage015/cc15aj.sc`。
経緯は [docs/stage017-gcc.md](../docs/stage017-gcc.md) 8.13。

## 直したもの 1: 括弧で囲んだ typedef 名

```c
typedef const char* (lto_get_section_data_f) (struct lto_file_decl_data *,
                                              enum lto_section_type,
                                              const char *, size_t *);
```

GCC の `lto-streamer.h` の形である。C89 6.5.4 の宣言子は「( declarator )」を含み，
括弧は意味を変えない。`typedef1()` は名前の位置に `(` を見て構文エラー (1) に
なっていた (gcc/ の 26 単位)。ふつうの宣言 (`dcont()`) と同じく
`isparennm()` / `unparen1()` / `unparen2()` で括弧を外す。

## 直したもの 2: 局所名が typedef 名を隠す

```c
typedef struct partition_def *partition;          /* partition.h */

static int basevar_index (var_map map, int partition)
{
  gcc_checking_assert (partition >= 0 && partition <= ...);
```

C89 6.1.2.1 の有効範囲の規則では，内側で宣言した名前は外側の同じ名前を隠す。
typedef 名も例外ではない。`istype()` は typedef 表だけを見ていたので，
`(partition >= 0 ...` をキャストと読んで 1 で止まっていた (gcc/ の 21 単位)。
局所変数 `edge` (typedef 名 `edge` と同じ) で始まる式文 `edge = ...;` も
宣言と読んでいた (6 単位)。

関数本体の中では，file scope の typedef 名と同じ名前の局所名 (仮引数を含む)
があれば型名と見なさない。局所表は関数を抜けても次の関数まで残るので，
関数本体の中かどうかを `infn` で持つ。

**名前の有効範囲は宣言子を読み終えた直後から始まる** (C89 6.1.2.1)。
`typedef double real;` の下の `int real[sizeof (real)];` では，大きさの中の
`real` はまだ typedef 名 (double) を指す。`plocal()` は `pdims()` より先に
局所名を登録するので，宣言子を読んでいる間はその局所名を遮蔽に数えない
(`ldcur`)。最初の版はこれを数えていて，配列を 4 要素で組んでいた
(自動レビューの指摘で直した)。

**限界。** 関数の中で宣言した typedef と局所名の前後関係は持っていない。
関数の中の typedef は従来どおり型名と見る。

## 直したもの 3: typedef 表を内側から引く

`tdfind()` は外側 (先に登録した方) から引いていた。内側の複文で外側と同じ
名前を typedef すると，外側の型が返った。局所名の `lfind()` と同じく内側から
引く。同じ複文の中での重複の検査 (`tdfind() >= tdblk`) も，これで内側の
宣言を見るようになる。

## ビルドチェーンは変わらない

既存のソースは typedef 名と同じ名前の局所名を使わず，括弧で囲んだ typedef 名も
書かない。`sh` / `ed` / `mk` を訳した `.o` は `cc10l` のものと 1 バイトも変わらない。

## 測り方

`tests/stage015/probe/tdshadow.c` を足した。**型名と読めばキャストや宣言になり，
式と読めば値になる。** 仮引数と局所変数による遮蔽，複文を抜けたあとの型名の
復帰，内側の typedef による外側の typedef の遮蔽，括弧で囲んだ関数型の
typedef，宣言子の中での typedef 名 (`int real[sizeof (real)]`) を並べ，値を
ホストと突き合わせる。

| | `cc15ai` | `cc15aj` |
|---|---|---|
| `tools/diff17.sh tdshadow` | **我々だけが拒む (rc=1)** | 値が一致 (`8 -1 321 12 1044 9 42 4 35`) |

## ビルド

```
sh tools/build.sh stage015
# cc15ai(cc15aj.sc) -> cc15aj0    (1 段目)
# cc15aj0(cc15aj.sc) -> cc15aj    (2 段目。以降は固定点)
```

SHA-256: e5a98be3c766e430c2d54dd120c7547c9210a13e575666936fb9ec820b79261a

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)
