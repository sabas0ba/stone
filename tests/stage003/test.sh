#!/bin/bash
# Stage 3 テスト: asm の検証 (docs/stage003-asm.md 5 章)。
#
# 検証項目:
#   1. ビルド再現: hex1(asm.hex1) の SHA-256 が asm.md 記載値と一致
#   2. 全命令エンコード: asm(rv32im.s) == hex0(rv32im-expected.hex)
#   3. 自己アセンブル固定点: asm(asm.hex1 の転記) == asm.bin
#   4. 実行: asm(run.s) が QEMU 上で "OK" を出力し終了コード 0
#   5. エラー系: 未知ニーモニック -> 2, 未定義ラベル -> 3, 重複定義 -> 4,
#      即値範囲外 -> 6, 不正レジスタ -> 7
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

hex0=stage001/hex0.bin
asm=tmp/build/asm.bin
doc=stage003/asm.md

# 1. ビルド再現
ensure_build stage003
rc=$?
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$asm")
actual=${actual%% *}
[ "$rc" -eq 0 ] && [ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "build: hex1(asm.hex1) の SHA-256 が asm.md 記載値と一致"

# 2. 全命令エンコード
sh tools/env.sh qemu "$asm" < tests/stage003/rv32im.s > tmp/rv32im-asm.bin \
    && sh tools/env.sh qemu "$hex0" < tests/stage003/rv32im-expected.hex > tmp/rv32im-exp.bin \
    && cmp -s tmp/rv32im-asm.bin tmp/rv32im-exp.bin
report $? "encode: asm(rv32im.s) が手エンコードの期待バイナリと一致"

# 3. 自己アセンブル固定点 (転記コメントの抽出は sed による機械抽出)
sed -n 's/.*#: //p' stage003/asm.hex1 > tmp/asm-self.s
sh tools/env.sh qemu "$asm" < tmp/asm-self.s > tmp/asm-self.bin \
    && cmp -s tmp/asm-self.bin "$asm"
report $? "self: asm(asm.hex1 の転記) が asm.bin とビット一致"

# 4. 実行
# アセンブル自体の成否を独立して報告する。まとめてしまうと，アセンブラが
# 落ちたのか生成物の実行結果が違うのかが読めない
sh tools/env.sh qemu "$asm" < tests/stage003/run.s > tmp/run-s.bin
rc=$?
report $rc "assemble: asm(run.s) が終了コード 0 でアセンブルできる"

[ "$rc" -eq 0 ] && sh tools/env.sh qemu tmp/run-s.bin < /dev/null > tmp/run-s.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/run-s.out)" = "OK" ]
report $? "run: asm(run.s) の実行 (stdout 'OK', 終了コード 0)"

# 5. エラー系
printf 'qq x1 x2 x3 .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 2 ]
report $? "error: 未知ニーモニックで終了コード 2"

printf 'j nowhere .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 3 ]
report $? "error: 未定義ラベルで終了コード 3"

printf 'a: a: .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 4 ]
report $? "error: 重複定義で終了コード 4"

printf 'addi x1 x2 4096 .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 6 ]
report $? "error: 即値範囲外で終了コード 6"

printf 'add x32 x1 x2 .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 7 ]
report $? "error: 不正レジスタで終了コード 7"

# 名前の 15 バイト超過は構文の誤り (コード 1)
printf 'averyveryverylonglabel: .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 1 ]
report $? "error: 名前の長さ超過で終了コード 1"

# コード 5 は距離超過だけでなく奇数オフセットでも出る。ラベルを置いてから
# 1 バイト詰めると分岐命令との差が奇数になるので，距離を稼がずに発火する
printf 'a: byte 0 beq x0 x0 a .' | sh tools/env.sh qemu "$asm" > /dev/null
[ $? -eq 5 ]
report $? "error: 奇数オフセットの分岐で終了コード 5"

summary
