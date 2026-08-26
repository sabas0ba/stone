#!/bin/bash
# Stage 7 テスト: occ (現代化) の検証 (docs/stage007-occ.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: B2 (occ.bin) の SHA-256 が occ.md 記載値と一致
#   2. 固定点: B3 = B2(occ.sc) が B2 とビット一致 (完了条件)
#   3. 同値性: Stage 5 仕様スイートを B2 でコンパイル・実行
#   4. パス単体テスト: fold / dce (コンパイル結果のビット一致), 出力サイズ改善
#   5. エラー系: 1..6
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp
stable_dir=tmp/stable7

occ=tmp/build/occ.bin
doc=stage007/occ.md

csc() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$occ" > "$2"
}
cstr() {
    printf "$1\004" | sh tools/env.sh qemu "$occ" > "$2"
}

# 1. ビルド再現
ensure_build stage007
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$occ")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: B2 (occ.bin) の SHA-256 が occ.md 記載値と一致"

# 2. 固定点
#
# **QEMU を通す実行は CI で稀に揺らぐ** (docs/dev-notes.md 1.6)。
# 素の cmp だと，中身の退行と実行の非再現が同じ FAIL に見える
fp7gen() {
    { cat stage007/occ.sc; printf '\004'; } | sh tools/env.sh qemu "$occ" > "$1"
}
stable_cmp "fixpoint(occ)" fp7gen "$occ"
report $? "fixpoint: B3 = B2(occ.sc) が B2 とビット一致 (完了条件)"

# 3. 同値性: Stage 5 仕様スイート
run_case() {
    name=$1
    expect=$2
    csc "tests/stage005/$name.sc" "tmp/o7-$name.bin" \
        && sh tools/env.sh qemu "tmp/o7-$name.bin" < /dev/null > "tmp/o7-$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/o7-$name.out")" = "$expect" ]
    report $? "equiv: $name.sc (B2 でコンパイル)"
}

run_case arith "oooooooooooooooo"
run_case fib "610"
run_case ptr "oooHI"
run_case struct "oopt"

csc tests/stage005/upper.sc tmp/o7-upper.bin \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/o7-upper.bin > tmp/o7-upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/o7-upper.out)" = "HELLO WORLD 123" ]
report $? "equiv: upper.sc (B2 でコンパイル)"

# 4. パス単体テスト
cstr 'int main() { return 2 + 3 * 4; }' tmp/u1a.bin
cstr 'int main() { return 14; }' tmp/u1b.bin
cmp -s tmp/u1a.bin tmp/u1b.bin
report $? "fold: 2 + 3 * 4 が 14 と同一バイナリ"

cstr 'int main() { return -(2 + 3); }' tmp/u2a.bin
cstr 'int main() { return -5; }' tmp/u2b.bin
cmp -s tmp/u2a.bin tmp/u2b.bin
report $? "fold: -(2 + 3) が -5 と同一バイナリ"

cstr 'int main() { 1 + 2; return 7; }' tmp/u3a.bin
cstr 'int main() { return 7; }' tmp/u3b.bin
cmp -s tmp/u3a.bin tmp/u3b.bin
report $? "dce: 捨てられる式 1 + 2 の除去"

printf 'int main() { return 2 + 3 * 4; }\004' | sh tools/env.sh qemu tmp/build/scc.bin > tmp/u4.bin
[ "$(stat -c%s tmp/u1a.bin)" -lt "$(stat -c%s tmp/u4.bin)" ]
report $? "codegen: occ の出力が scc の出力より小さい"

# 4KiB を超える分岐 (occ の主眼。docs/stage007-occ.md 3 章)。
# 旧実装 (sc/scc) は B-type 即値 (±4KiB) の範囲検査をせずに beq を直接
# 出すため，本体が 4KiB を超える if を silent に誤コンパイルする。occ は
# 「逆条件の B-type で 1 語跳び越え + jal」に落として制限を外した。
#
# occ.sc 自身は「旧コード生成でも ±4KiB に収まるよう」大きなループ本体を
# 関数へ分割して書いてあるので (同 3 章)，固定点検証ではこの経路を通らない。
# ここだけが唯一の検査になる。
#
# **条件を偽にして本体を跨がせる**のが要点である。真にして本体へ落ちる形だと
# 分岐そのものが取られず，飛び先が壊れていても素通りしてしまう (検査になら
# ない)。同じソースを scc でコンパイルすると，切り詰められた飛び先へ跳んで
# 戻らなくなる —— それが occ を作った理由である
{
    echo 'int main() {'
    echo '  int x;'
    echo '  x = 5;'
    echo '  if (x == 0) {'
    i=0
    while [ $i -lt 1200 ]; do echo '    x = x + 1;'; i=$((i + 1)); done
    echo '  }'
    echo '  return x + 2;'
    echo '}'
} > tmp/bigbr.sc
csc tmp/bigbr.sc tmp/bigbr.bin
rc=$?
# 本体が 4KiB を超えていること自体を確かめる (超えていなければ検査になら
# ない)。生成物全体の大きさで代用する
[ "$rc" -eq 0 ] && [ "$(stat -c%s tmp/bigbr.bin)" -gt 4096 ]
report $? "branch: 本体 4KiB 超の if を含むソースがコンパイルでき，出力が 4KiB を超える"

sh tools/env.sh qemu tmp/bigbr.bin < /dev/null > /dev/null
[ $? -eq 7 ]
report $? "branch: 4KiB 超の本体を跨ぐ分岐が正しく飛ぶ (終了コード 7)"

# 5. エラー系
esc() {
    printf "$1\004" | sh tools/env.sh qemu "$occ" > /dev/null
    [ $? -eq "$2" ]
    report $? "error: $3 で終了コード $2"
}
esc 'int main() { return 0 }' 1 "構文エラー"
esc 'int main() { return foo; }' 2 "未定義の識別子"
esc 'int f() { return 0; }' 3 "main 未定義"
esc 'int x; int x; int main() { return 0; }' 4 "重複定義"
esc 'int main() { 3 = 4; return 0; }' 5 "非左辺値への代入"
esc 'int main() { char b[3000]; return 0; }' 6 "フレーム容量超過"

summary
