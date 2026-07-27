# 開発ノート (環境・デバッグ手順)

本書は [plan.md](plan.md) を補う実務メモである。計画・仕様ではなく，
「作業を再開する人が知らないと時間を溶かす」種類の情報を集めている。

- 1 章: コンテナが使えない環境での作業手順
- 2 章: デバッグ手法 (実際にバグを見つけた手順を残す)
- 3 章: 検証レシピ (固定点・コメントのみ変更の証明)
- 4 章: 落とし穴

## 1. 実行環境

### 1.1 標準はコンテナ

[plan.md](plan.md) 2.3 のとおり，再現性の基準はコンテナ (`env/Containerfile`) である。
CI は毎回このイメージを構築し，全 Stage のテストを実行する。
**各 `.md` に記録した SHA-256 の正当性を保証するのは CI である。**

```
sh tools/env.sh build      # イメージ構築と packages.lock の照合
bash tools/test.sh         # 全 Stage のテスト
```

### 1.2 コンテナが使えない場合 (egress 制限のある環境)

作業環境によっては `env.sh build` が失敗する。これまでに確認した例:

| 症状 | 原因 |
|---|---|
| `Cannot connect to the Docker daemon` | dockerd が起動していない。`dockerd &` で起動する |
| ベースイメージ取得が 403 | `production.cloudfront.docker.com` が egress ポリシーで拒否されている |
| `apt-get update` が 403 (`x-deny-reason: host_not_allowed`) | `snapshot.debian.org` が拒否されている。**ポート 80 のみ拒否**という例もある (443 は許可でも Containerfile は `http://` を使うため失敗する) |

`snapshot.debian.org` は迂回できない。plan.md 2.3 でパッケージをアーカイブ時点に
固定しており，`deb.debian.org` 等へ差し替えると版が動いて `env/packages.lock` との
照合が壊れる。HTTPS へ切り替える案も，ベースイメージ (`debian:bookworm-slim`) が
`ca-certificates` を持たないため成立しない (それを入れるには apt が要る，という循環)。
**egress を許可してもらうか，本節の手順でホスト実行に切り替えるかの二択である。**

#### ホストでの実行

```
apt-get install -y qemu-system-misc binutils-riscv64-unknown-elf
```

`tools/env.sh` に以下の分岐を一時的に入れると，既存のテスト・ビルドスクリプトを
そのまま `STONE_ENGINE=host` で流用できる。

```sh
if [ "${STONE_ENGINE:-}" = host ]; then
    repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
    cmd=${1:-}; shift || true
    case "$cmd" in
    qemu) exec sh "$repo_root/tools/run-qemu.sh" "$@" ;;
    run)  cd "$repo_root" && exec "$@" ;;
    build) exit 0 ;;
    esac
fi
```

> **このパッチは絶対に commit しないこと。** ローカルの便宜であり，
> コンテナを基準とする再現性ポリシーを迂回する。作業後は元に戻し，
> commit 前に `git diff tools/env.sh` が空であることを確認する。
>
> (恒久的にサポートする案もあるが，`env.sh` の契約を変えることは
> 再現性ポリシーの変更にあたるため，plan.md の改訂とセットで判断する。)

ホスト実行の妥当性: Stage 2 以降のすべての `.md` 記載 SHA-256 は，
ホスト QEMU で算出した値をコンテナ上の CI が照合する形で運用してきた。
これまで両者が食い違ったことはない。ただしホストの QEMU は
`env/packages.lock` で固定した版とは限らないため，**最終的な正当性は
常に CI 側に置く**こと。

## 2. デバッグ手法

生成物は裸の RV32 バイナリで，デバッガから見える名前も型もない。
以下は実際にバグを特定した手順である。

### 2.1 固有エラーコードの注入 (どの検査で落ちたかを知る)

処理系は異常時に `exit(N)` で停止するが，同じコード N を返す箇所が多数あるため
「どの検査に引っかかったか」が判らない。ソースを機械変換して，各 `exit` 箇所に
互いに異なるコードを振ったデバッグ版を作ると，終了コードがそのまま発生位置になる。

```sh
python3 - <<'EOF'
lines = open('stage005/sc.sol').read().splitlines()
n, out, mapping = [10], [], []
for i, ln in enumerate(lines):
    while '1 exit' in ln:
        code = n[0]; n[0] += 1
        ln = ln.replace('1 exit', f'{code} exit', 1)
        mapping.append((code, i + 1))
    out.append(ln)
open('tmp/sc-debug.sol', 'w').write('\n'.join(out) + '\n')
for c, l in mapping: print(c, 'line', l)
EOF
```

これで `stage005/sc.sol` の「引数個数の検査漏れ」を特定した。

### 2.2 実行トレースと PC 周期 (無限ループの原因を知る)

```sh
qemu-system-riscv32 -M virt -bios tmp/x.bin -display none -monitor none \
    -serial stdio -singlestep -d exec 2> tmp/trace.log
```

- `-singlestep` が必要 (これがないと翻訳ブロック単位でしか記録されない)
- `-one-insn-per-tb` は本バージョンの QEMU には**存在しない**

PC 列を抽出して繰り返しを見つける:

```sh
grep -o '/00000000800[0-9a-f]*/' tmp/trace.log | sed 's|/00000000||;s|/||' > tmp/pcs.txt
tail -600 tmp/pcs.txt | sort | uniq -c | sort -rn | head    # 周回している範囲
```

得られたアドレスを `verify/disasm.sh` の逆アセンブル結果と突き合わせる。
この手順で「B-type 分岐の ±4KiB 制限による silent 誤コンパイル」を特定した
([stage007-occ.md](stage007-occ.md) 3 章)。

### 2.3 大域アドレスから変数名への逆引き

生成コードに現れる `lui`/`addi` の即値は大域変数の絶対アドレスである。
ソースの大域宣言を順に読んで割付けを再現すれば，どの変数かが判る。

```sh
python3 - <<'EOF'
import re
addr = 0x80100000     # 生成プログラムの BSS 先頭
target = 0x801e7168   # 逆アセンブルに現れたアドレス
head = open('stage007/occ.sc').read().split('int init()')[0]
for m in re.finditer(r'(char|int)\s+(\*?)(\w+)(\[(\d+)\])?;', head):
    ty, star, name, _, n = m.groups()
    size = ((int(n) * (1 if (ty == 'char' and not star) else 4)) + 3) & ~3 if n else 4
    if addr <= target < addr + size:
        print(f'{name} + {target - addr}')
    addr += size
EOF
```

これで「ポインタ演算のスケーリングが効いていない」箇所を特定した。

## 3. 検証レシピ

### 3.1 固定点の確認

セルフホストした処理系 (Stage 6 以降) を触ったら必ず実行する。

```sh
sh tools/build.sh stage007          # B1, B2 を生成
{ cat stage007/occ.sc; printf '\004'; } \
    | sh tools/env.sh qemu tmp/build/occ.bin > tmp/b3.bin
cmp tmp/build/occ.bin tmp/b3.bin    # B2 == B3 が完了条件
```

sc 系の入力終端は **EOT (0x04)** である (`.` ではない)。`printf '\004'` の付加を忘れると
入力待ちのまま止まる。

### 3.2 「コメントのみの変更」の証明

コメントを増やしたときは，コードに触れていないことを機械的に示す。

```sh
strip() { sed 's|//.*||' "$1" | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//' | grep -v '^$'; }
git show HEAD:stage007/occ.sc > tmp/old.sc
diff <(strip tmp/old.sc) <(strip stage007/occ.sc) && echo "code identical"
```

sol (`stage005/sc.sol`) はコメントが `#` なので `sed 's|#.*||'` に読み替える。
併せて全チェーンを再ビルドし，各 `.md` の SHA-256 と一致することを確認する。

## 4. 落とし穴

- **`xxd` が入っていない。** バイナリを見るときは `od -A x -t x1` を使う。
- **全 Stage のテストは 10 分を超える。** フォアグラウンドで実行すると打ち切られる
  ことがある。バックグラウンド実行するか Stage 単位で流す。
- **`stage007/occ.sc` は scc がコンパイルできる形を保つ必要がある。**
  ブートストラップ 1 段目 (`B1 = scc(occ.sc)`) は旧コード生成を通るため，
  `if`/`while` の本体が 4KiB を超えると B1 が壊れる。大きなループ本体は
  関数に切り出す (`fold` に対する `foldins` がその例)。
- **文書化コメントの形式は言語で異なる。** sc 系 (`scc.sc` / `occ.sc`) は Doxygen の
  `///` がそのまま使える。sol (`sc.sol`) は行コメントが `#` のため `#/` を
  文書化コメントとする規約を用いる (Doxygen へ掛けるなら `FILTER_PATTERNS` で
  `#/` → `///`)。sol はスタック指向で名前付き引数がないため `@param` は使わず，
  スタック効果を `@stack ( 引数 -- 結果 )` で記す。
- **hex0/hex1/asm/sol の入力終端は `.`，sc 系は EOT (0x04)。** Stage 5 で変わっている。
  Stage 9 の pp は入力・出力ともに EOT で，`pp | cc` と繋げられる。
- **pp の 1 文字先読み (押し戻し) はフレームごとに区切る。** マクロ名の次の
  文字を読んでから展開フレームを積むため，押し戻しを共有すると展開結果より
  先にその文字が出てしまう (`return A;` が `return ;1` になる)。フレームを
  積むときに押し戻しの床を上げ，降ろすときに戻す。あわせて，フレームを
  降ろした直後は押し戻しを再確認してから次のフレームを読む必要がある。
- **pp で `#if` の式を評価すると行頭性が落ちる。** 式の展開はフレームからの
  読取りなので，`atbol` が 0 になったまま指令処理を抜けてしまう。抜ける際に
  必ず立て直す。怠ると次の行の指令が本文として素通しされ，`#if` の入れ子が
  合わなくなる。
- **番兵フレームは自分では降ろさない。** 展開しきりの終端で `ungc(-1)` を
  受け止める先が要るため，番兵は終端に達しても積んだままにし，読み終えた側が
  明示的に降ろす。先に降ろすと -1 が一段下の押し戻しへ紛れ込む。
