#!/bin/bash
# 独立監査: 鎖の信頼根 (Stage 1〜3) を，鎖自身を使わずに検証する。
# 背景と検査の一覧は README.md を参照。
#
# 各検査は仕様書から独立に書き起こした参照実装 (hex0.py / hex1.py / asm.py)
# で成果物・記録 SHA-256 を再導出し，git 管理下の記録値と突き合わせる。
# QEMU もコンテナも使わない (ホスト側の python3 のみ)。読取り専用であり，
# ビルド成果物には影響しない (docs/plan.md 2.2 の verify 層と同じ位置づけ)。
#
# 使用法: bash verify/audit/audit.sh
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
work=tmp/audit-ref
mkdir -p "$work"

py() { python3 "verify/audit/$1"; }

# .md 記載の SHA-256 (回帰ピン) を取り出す
recorded() { grep -Eo '^SHA-256: [0-9a-f]{64}' "$1" | cut -d' ' -f2; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

section "seed (stage001)"

[ "$(sha stage001/hex0.bin)" = "$(recorded stage001/hex0.md)" ]
report $? "seed: hex0.bin が hex0.md 記載の SHA-256 と一致"

py hex0.py < stage001/hex0.hex > "$work/hex0.bin"
rc=$?
[ "$rc" -eq 0 ] && cmp -s "$work/hex0.bin" stage001/hex0.bin
report $? "seed: 参照実装 hex0(hex0.hex) が hex0.bin とビット一致 (listing の独立再導出)"

py hex0.py < tests/stage001/hello.hex > "$work/hello.bin"
rc=$?
[ "$rc" -eq 0 ] && cmp -s "$work/hello.bin" tests/stage000/hello.bin \
    && [ "$(sha tests/stage000/hello.bin)" = "$(recorded tests/stage000/hello.md)" ]
report $? "seed: 参照実装 hex0(hello.hex) が hello.bin とビット一致し記録 SHA とも一致"

section "hex1 (stage002)"

py hex0.py < stage002/hex1.hex > "$work/hex1.bin"
rc=$?
[ "$rc" -eq 0 ] && [ "$(sha "$work/hex1.bin")" = "$(recorded stage002/hex1.md)" ]
report $? "hex1: 参照実装 hex0(hex1.hex) の SHA-256 が hex1.md 記載値と一致 (記録の独立再導出)"

py hex1.py < stage001/hex0.hex > "$work/hex0-via-hex1.bin"
rc=$?
[ "$rc" -eq 0 ] && cmp -s "$work/hex0-via-hex1.bin" stage001/hex0.bin
report $? "hex1: 参照実装 hex1(hex0.hex) が hex0.bin とビット一致 (上位互換の独立確認)"

py hex1.py < tests/stage002/labels.hex1 > "$work/labels.bin"
rc=$?
py hex0.py < tests/stage002/labels-expected.hex > "$work/labels-exp.bin"
rc2=$?
[ "$rc" -eq 0 ] && [ "$rc2" -eq 0 ] && cmp -s "$work/labels.bin" "$work/labels-exp.bin"
report $? "hex1: 参照実装でラベル機能ゴールデン (labels-expected.hex) を再現"

section "asm (stage003)"

py hex1.py < stage003/asm.hex1 > "$work/asm.bin"
rc=$?
[ "$rc" -eq 0 ] && [ "$(sha "$work/asm.bin")" = "$(recorded stage003/asm.md)" ]
report $? "asm: 参照実装 hex1(asm.hex1) の SHA-256 が asm.md 記載値と一致 (記録の独立再導出)"

py asm.py < tests/stage003/rv32im.s > "$work/rv32im.bin"
rc=$?
py hex0.py < tests/stage003/rv32im-expected.hex > "$work/rv32im-exp.bin"
rc2=$?
[ "$rc" -eq 0 ] && [ "$rc2" -eq 0 ] && cmp -s "$work/rv32im.bin" "$work/rv32im-exp.bin"
report $? "asm: RV32IM 全命令ゴールデン (rv32im-expected.hex) を参照実装で独立再エンコード"

# 転記コメント (#:) の抽出は tests/stage003/test.sh と同一の手順
sed -n 's/.*#: //p' stage003/asm.hex1 > "$work/asm-self.s"
py asm.py < "$work/asm-self.s" > "$work/asm-self.bin"
rc=$?
[ "$rc" -eq 0 ] && cmp -s "$work/asm-self.bin" "$work/asm.bin"
report $? "asm: 自己アセンブル固定点 asm(転記) == hex1(asm.hex1) を参照実装のみで再現"

section "鎖の成果物との照合 (tmp/build がある場合のみ)"

if [ -f tmp/build/hex1.bin ] && [ -f tmp/build/asm.bin ]; then
    cmp -s "$work/hex1.bin" tmp/build/hex1.bin
    report $? "chain: 参照実装の hex1.bin が鎖の生成物とビット一致"
    cmp -s "$work/asm.bin" tmp/build/asm.bin
    report $? "chain: 参照実装の asm.bin が鎖の生成物とビット一致"
else
    echo "skip チェーン生成物が無い (sh tools/build.sh stage003 の後に再実行すると照合する)"
fi

summary
