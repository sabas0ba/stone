#!/bin/sh
# verify 層: nas1 が組み立てた命令を逆アセンブルし，入力の "# expect:" と照合する
# (docs/stage018-ncc.md 7 章の 1)。読取り専用の検証であり，ビルド成果物には
# 影響しない (docs/plan.md 2.2)。
#
# 使用法: nas1insn.sh <repo 相対パスの .o> <入力の .s>
#
# 期待は入力の各行の "# expect:" の後ろで，" | " で区切ると複数の命令になる。
# objdump の行は「命令 被演算子」に直し，分岐と jal の行き先 (絶対番地) を
# 命令からの相対 (.+N / .-N) に直してから比べる。<記号> の注記と # 以降の
# 注釈は捨てる。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
obj=$1
src=$2
tmp="$repo_root/tmp/verify-nas1"
mkdir -p "$tmp"

sed -n 's/.*# expect: //p' "$src" | awk -F ' [|] ' '{ for (i = 1; i <= NF; i++) print $i }' \
    > "$tmp/want"

sh "$repo_root/tools/env.sh" run riscv64-unknown-elf-objdump -d \
    -M no-aliases,numeric -j .text "$obj" \
    | awk '
        # mawk には strtonum が無いので 16 進は自分で読む
        function hex(s,    i, c, v) {
            v = 0
            for (i = 1; i <= length(s); i++) {
                c = index("0123456789abcdef", substr(s, i, 1))
                v = v * 16 + c - 1
            }
            return v
        }
        # "   1c:\t1405a52f          \tlr.w.aq\tx10,(x11)" の形の行だけを読む
        /^ *[0-9a-f]+:\t[0-9a-f]+ / {
            n = split($0, f, "\t")
            addr = f[1]; sub(/^ */, "", addr); sub(/:$/, "", addr)
            op = f[3]; ops = (n >= 4) ? f[4] : ""
            sub(/ *<[^>]*>/, "", ops)
            sub(/ *#.*/, "", ops)
            if (op ~ /^(beq|bne|blt|bge|bltu|bgeu|jal)$/) {
                k = split(ops, a, ",")
                d = hex(a[k]) - hex(addr)
                # 番地 0 より前への行き先は 32 bit で折り返して表示される
                if (d >= 2147483648) d -= 4294967296
                a[k] = (d < 0) ? ".-" (-d) : ".+" d
                ops = a[1]
                for (i = 2; i <= k; i++) ops = ops "," a[i]
            }
            print (ops == "") ? op : op " " ops
        }' > "$tmp/got"

if diff -u "$tmp/want" "$tmp/got" > "$tmp/diff"; then
    echo "nas1insn: $(wc -l < "$tmp/want" | tr -d ' ') 命令が一致"
    exit 0
fi
cat "$tmp/diff"
exit 1
