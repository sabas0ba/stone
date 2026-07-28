#!/bin/bash
# Stage 8 テスト: ELF オブジェクト + リンカの検証 (docs/stage008-elf-ld.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: cc.bin / ld.bin の SHA-256 が各 .md 記載値と一致
#   2. 固定点: cc / ld がそれぞれ自分自身を再生成する
#   3. 分割コンパイル: 2 つの翻訳単位に分けたプログラムが動作する
#   4. 同値性: Stage 5 の仕様スイートが cc + ld で動作する
#   5. ELF 妥当性 (verify 層): readelf がオブジェクトを解釈できる
#   6. エラー系: 未定義 -> 2, 多重定義 -> 3, main 未定義 -> 5, 非 ELF -> 1
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s8

cc=tmp/build/cc8.bin
ld=tmp/build/ld.bin

# ソースを ELF オブジェクトへ (sc の入力終端は EOT)
compile() {
    { cat "$1"; printf '\004'; } | sh tools/env.sh qemu "$cc" > "$2"
}
# オブジェクト列を実行像へ (列の終わりは 0x7f 以外の 1 バイト)
link() {
    out=$1
    shift
    { cat "$@"; printf '\0'; } | sh tools/env.sh qemu "$ld" > "$out"
}

# 1. ビルド再現
ensure_build stage008
rc=$?
ok=0
[ "$rc" -eq 0 ] || ok=1
for pair in cc8:stage008/cc.md ld:stage008/ld.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.bin"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc8.bin / ld.bin の SHA-256 が各 .md 記載値と一致"

# 2. 固定点
compile stage008/cc.sc tmp/s8/cc2.o && link tmp/s8/cc2.bin tmp/s8/cc2.o \
    && cmp -s tmp/s8/cc2.bin "$cc"
report $? "fixpoint: cc8 が自分自身を再生成する"

compile stage008/ld.sc tmp/s8/ld2.o && link tmp/s8/ld2.bin tmp/s8/ld2.o \
    && cmp -s tmp/s8/ld2.bin "$ld"
report $? "fixpoint: ld が自分自身を再生成する"

# 3. 分割コンパイル (split-a が split-b の関数を，split-b が文字列と putc を使う)
compile tests/stage008/split-a.sc tmp/s8/a.o \
    && compile tests/stage008/split-b.sc tmp/s8/b.o \
    && link tmp/s8/split.bin tmp/s8/a.o tmp/s8/b.o \
    && sh tools/env.sh qemu tmp/s8/split.bin < /dev/null > tmp/s8/split.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s8/split.out)" = "val=7val=10" ]
report $? "split: 2 翻訳単位の個別コンパイルとリンク"

# リンク順を入れ替えても同じ結果になること (前方・後方参照の両方を通る)
link tmp/s8/split2.bin tmp/s8/b.o tmp/s8/a.o \
    && sh tools/env.sh qemu tmp/s8/split2.bin < /dev/null > tmp/s8/split2.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s8/split2.out)" = "val=7val=10" ]
report $? "split: リンク順を入れ替えても動作する"

# 4. 同値性 (Stage 5 仕様スイート)
run_case() {
    name=$1
    expect=$2
    compile "tests/stage005/$name.sc" "tmp/s8/$name.o" \
        && link "tmp/s8/$name.bin" "tmp/s8/$name.o" \
        && sh tools/env.sh qemu "tmp/s8/$name.bin" < /dev/null > "tmp/s8/$name.out"
    rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "tmp/s8/$name.out")" = "$expect" ]
    report $? "equiv: $name.sc"
}
run_case arith "oooooooooooooooo"
run_case fib "610"
run_case ptr "oooHI"
run_case struct "oopt"

compile tests/stage005/upper.sc tmp/s8/upper.o \
    && link tmp/s8/upper.bin tmp/s8/upper.o \
    && printf 'hello World 123.' | sh tools/env.sh qemu tmp/s8/upper.bin > tmp/s8/upper.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/s8/upper.out)" = "HELLO WORLD 123" ]
report $? "equiv: upper.sc"

# 5. ELF 妥当性 (verify 層。読取り専用であり成果物には影響しない)
sh tools/env.sh run riscv64-unknown-elf-readelf -h tmp/s8/a.o > tmp/s8/elf.txt 2>&1 \
    && grep -q 'REL (Relocatable file)' tmp/s8/elf.txt \
    && grep -q 'RISC-V' tmp/s8/elf.txt
report $? "verify: readelf が ET_REL / RISC-V として解釈できる"

sh tools/env.sh run riscv64-unknown-elf-readelf -rW tmp/s8/b.o > tmp/s8/rel.txt 2>&1 \
    && grep -q 'R_RISCV_HI20' tmp/s8/rel.txt \
    && grep -q 'R_RISCV_LO12_I' tmp/s8/rel.txt \
    && grep -q 'R_RISCV_JAL' tmp/s8/rel.txt
report $? "verify: readelf が HI20 / LO12_I / JAL 再配置を解釈できる"

# 6. エラー系
printf 'int main() { nosuch(); return 0; }\004' | sh tools/env.sh qemu "$cc" > tmp/s8/e1.o
link /dev/null tmp/s8/e1.o
[ $? -eq 2 ]
report $? "error: 未定義シンボルで終了コード 2"

printf 'int dup1() { return 1; } int main() { return dup1(); }\004' \
    | sh tools/env.sh qemu "$cc" > tmp/s8/e2.o
printf 'int dup1() { return 2; }\004' | sh tools/env.sh qemu "$cc" > tmp/s8/e3.o
link /dev/null tmp/s8/e2.o tmp/s8/e3.o
[ $? -eq 3 ]
report $? "error: 多重定義で終了コード 3"

printf 'int f() { return 0; }\004' | sh tools/env.sh qemu "$cc" > tmp/s8/e4.o
link /dev/null tmp/s8/e4.o
[ $? -eq 5 ]
report $? "error: main 未定義で終了コード 5"

printf '\177XYZ' | sh tools/env.sh qemu "$ld" > /dev/null
[ $? -eq 1 ]
report $? "error: ELF でない入力で終了コード 1"

summary
