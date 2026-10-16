#!/bin/sh
# verify 層: nas1 が出した ELF の節・再配置・記号・中身を readelf で読み，
# 比べやすい行に直して出す (docs/stage018-ncc.md 7 章の 1)。読取り専用の
# 検証であり，ビルド成果物には影響しない (docs/plan.md 2.2)。
#
# 使用法: nas1elf.sh <repo 相対パスの .o> [中身を出す節...]
#
# 出す行:
#   section <名前> <型> <旗> size=<16 進> align=<10 進> entsize=<10 進>
#   reloc <節> <位置> <型> <記号> +<加数>
#   symbol <名前> <値> <大きさ> <型> <結合> <可視性> <節の名前 | UND | ABS | COM>
#   bytes <節> <16 進のバイト列>
# 節の記号 (STT_SECTION) と 0 番の記号は出さない。記号は名前の順に並べる。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
obj=$1
shift
re=riscv64-unknown-elf-readelf

{
    echo "=S"
    sh "$repo_root/tools/env.sh" run $re -S -W "$obj"
    echo "=R"
    sh "$repo_root/tools/env.sh" run $re -r -W "$obj"
    echo "=Y"
    sh "$repo_root/tools/env.sh" run $re -s -W "$obj"
    for s in "$@"; do
        echo "=X $s"
        sh "$repo_root/tools/env.sh" run $re -x "$s" "$obj"
    done
} | awk '
    function hex(s) { sub(/^0+/, "", s); return "0x" (s == "" ? "0" : s) }
    /^=S/ { mode = "S"; next }
    /^=R/ { mode = "R"; next }
    /^=Y/ { mode = "Y"; next }
    /^=X / { mode = "X"; xsec = $2; xb = ""; next }
    mode == "S" && /^ *\[ *[0-9]+\]/ {
        line = $0
        sub(/^ *\[ *[0-9]+\] */, "", line)
        n = split(line, f, " +")
        idx = $0; sub(/^ *\[ */, "", idx); sub(/\].*/, "", idx)
        if (idx + 0 == 0) next
        secname[idx + 0] = f[1]
        if (f[2] == "RELA" || f[2] == "SYMTAB" || f[2] == "STRTAB") next
        # 名前 型 番地 位置 大きさ ES [旗] Lk Inf Al
        flg = (n == 10) ? f[7] : ""
        printf "section %s %s %s size=%s align=%d entsize=%d\n", f[1], f[2], flg, hex(f[5]), f[n], strtonum16(f[6])
        next
    }
    function strtonum16(s,    i, v) {
        v = 0
        for (i = 1; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
        return v
    }
    mode == "R" && /^Relocation section/ {
        rsec = $3; gsub(/\047/, "", rsec); sub(/^\.rela/, "", rsec)
        next
    }
    mode == "R" && /^[0-9a-f]+ +[0-9a-f]+ +R_RISCV_/ {
        # 位置 情報 型 記号の値 記号の名前 + 加数
        name = $5; add = $7
        printf "reloc %s %s %s %s +%s\n", rsec, hex($1), $3, name, hex(add)
        next
    }
    mode == "Y" && /^ +[0-9]+: / {
        if ($4 == "SECTION" || $8 == "") next
        ndx = $7
        if (ndx ~ /^[0-9]+$/) ndx = secname[ndx + 0]
        sym[$8] = sprintf("symbol %s %s %d %s %s %s %s", $8, hex($2), $3, $4, $5, $6, ndx)
        next
    }
    mode == "X" && /^ +0x[0-9a-f]+ / {
        for (i = 2; i <= 5; i++) {
            if ($i !~ /^[0-9a-f]+$/) break
            w = $i
            while (length(w) > 0) { xb = xb " " substr(w, 1, 2); w = substr(w, 3) }
        }
        xout[xsec] = xb
        next
    }
    END {
        # 記号は名前の順 (記号表の並びは連想配列の順で決まるため比べない)
        k = 0
        for (s in sym) names[++k] = s
        for (i = 2; i <= k; i++) {
            t = names[i]
            for (j = i - 1; j >= 1 && names[j] > t; j--) names[j + 1] = names[j]
            names[j + 1] = t
        }
        for (i = 1; i <= k; i++) print sym[names[i]]
        for (s in xout) print "bytes " s xout[s]
    }'
