# cc15o 説明文書

cc15o は C コンパイラの Stage 15 第 6 部の世代である。
[cc15n.sc](cc15n.sc) を出発点に，**局所の構造体を式で初期化する宣言**を
実装した。黙って初期化を落とす (bad) 誤りが 1 つ直っている。

ソース [cc15o.sc](cc15o.sc) が正本である。

## ビルド

```
sh tools/build.sh stage015
# cc15n(cc15o.sc) -> cc15o0     (1 段目)
# cc15o0(cc15o.sc) -> cc15o     (正本。以降は固定点)
```

SHA-256: 50f8eb98eaf56cfb42981a335d0ab2e9c0b01d770e928b63ade138b20cfd0c04

- 対象: RV32IM，リトルエンディアン
- ロードアドレス: 0x8000_0000 (QEMU virt, `-bios`)

## 直したもの (bad)

`struct S d = 式;` という**局所**の宣言で，cc15n までは複写のコードを
出していなかった。変数は未初期化のまま残る。

| 形 | cc15n | cc15o |
|---|---|---|
| `struct P d = 大域;` | **黙って捨てる** | 複写する |
| `struct P d = *p;` | **黙って捨てる** | 複写する |
| `struct P d = f();` | **黙って捨てる** | 複写する |
| `union U d = 大域;` | **黙って捨てる** | 複写する |
| `struct P d = {5, 6};` | 正しい | 変わらず |
| `int d = 式;` | 正しい | 変わらず |

原因は `linit` に構造体の経路が無く，末尾の `stval` へ落ちて 4 バイト
だけ書いていたこと。代入の側 (`scopy`) には正しい経路があるので，
初期化もそれに合わせた。

```c
  if (isstru(t)) {
    if (ety != t) exit(5);
    scopy(emit(c_laddr, loff[i], 0), v, tsize(t));
    return 0;
  }
```

## なぜこれが自己ホストを塞いでいたか

tcc は宣言の後に定義が来たとき，属性をこう退避する
(tccgen.c の `patch_type`)。

```c
struct FuncAttr f = sym->type.ref->f;
```

ここがスタックのゴミになると `func_noreturn` が偶然立ち，tcc は
「noreturn の関数を呼んだ後」としてコード生成を止める。その結果，
T1 が翻訳した tcc では

```c
ST_FUNC void gen_le16(int i) { gen_le8(i); gen_le8(i >> 8); }
```

の 2 つめの呼出しが消え，`tok_str_alloc` は `return str;` の読み直しを
落として直前の呼出しの返り値を返した。`func_ctor` / `func_dtor` が
立てば `.init_array` / `.fini_array` へ余計な登録が入る。
追い方は [docs/stage015-tcc.md](../docs/stage015-tcc.md) 12.21〜12.22。

## 検査

- 固定点: cc15o が自分自身を翻訳した結果が 2 回目と一致する
- 回帰: sh / ed / mk・rt64 / rtfp・kernel16・pp16 / ld15・libc15 の
  8 ファイルが cc15n と**バイト一致する** `.o` になる (この修正が
  影響するのは「局所の構造体を式で初期化する」形だけで，我々の
  ソースにその形が無いことの裏付けでもある)
- 単体: [strinit](../tests/stage015/probe/strinit.c)

## まだ直っていないもの (bad)

構造体の**配置**に 2 つ残っている (docs/stage015-tcc.md 12.23)。
どちらも我々の世界の中では辻褄が合うので自己ホストは塞がない。
配置規則の作り直しは影響範囲が広いので，独立した世代で行う。

- ビットフィールドの記憶単位が常に `unsigned` (4 バイト)。
  `unsigned short` / `unsigned char` の宣言を無視する
- `long long` / `double` の整列が 4 バイト。ilp32 でも 8 が正しい
