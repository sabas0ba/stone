#!/bin/sh
# verify 層: nld1 が出した実行形式の入口・節・program header・大域の記号・
# 中身を readelf で読み，比べやすい行に直して出す (docs/stage018-ncc.md 7 章)。
# 読取り専用の検証であり，ビルド成果物には影響しない (docs/plan.md 2.2)。
#
# 使用法: nld1elf.sh <repo 相対パスの実行形式> [中身を出す節...]
#
# 出す行:
#   entry <16 進>
#   section <名前> <型> <旗> addr=<16 進> size=<16 進>   (記号表と文字列表を除く)
#   load vaddr=<16 進> paddr=<16 進> filesz=<16 進> memsz=<16 進> flags=<R/W/E>
#   symbol <名前> <値> <結合> <節の名前 | ABS | UND>   (大域と weak だけ，名前の順)
#   bytes <節> <16 進のバイト列>
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
exe=$1
shift
re=riscv64-unknown-elf-readelf

{
    echo "=H"
    sh "$repo_root/tools/env.sh" run $re -h "$exe"
    echo "=S"
    sh "$repo_root/tools/env.sh" run $re -S -W "$exe"
    echo "=L"
    sh "$repo_root/tools/env.sh" run $re -l -W "$exe"
    echo "=Y"
    sh "$repo_root/tools/env.sh" run $re -s -W "$exe"
    for s in "$@"; do
        echo "=X $s"
        sh "$repo_root/tools/env.sh" run $re -x "$s" "$exe"
    done
} | awk '
    function hex(s) { sub(/^0x/, "", s); sub(/^0+/, "", s); return "0x" (s == "" ? "0" : s) }
    /^=H/ { mode = "H"; next }
    /^=S/ { mode = "S"; next }
    /^=L/ { mode = "L"; next }
    /^=Y/ { mode = "Y"; next }
    /^=X / { mode = "X"; xsec = $2; xb = ""; next }
    mode == "H" && /Entry point address:/ { print "entry " hex($4); next }
    mode == "S" && /^ *\[ *[0-9]+\]/ {
        line = $0
        sub(/^ *\[ *[0-9]+\] */, "", line)
        n = split(line, f, " +")
        idx = $0; sub(/^ *\[ */, "", idx); sub(/\].*/, "", idx)
        if (idx + 0 == 0) next
        secname[idx + 0] = f[1]
        if (f[2] == "SYMTAB" || f[2] == "STRTAB") next
        flg = (n == 10) ? f[7] : ""
        printf "section %s %s %s addr=%s size=%s\n", f[1], f[2], flg, hex(f[3]), hex(f[5])
        next
    }
    mode == "L" && $1 == "LOAD" {
        # LOAD 位置 vaddr paddr filesz memsz 旗... 整列
        fl = ""
        for (i = 7; i < NF; i++) fl = fl $i
        printf "load vaddr=%s paddr=%s filesz=%s memsz=%s flags=%s\n", hex($3), hex($4), hex($5), hex($6), fl
        next
    }
    mode == "Y" && /^ +[0-9]+: / {
        if ($5 != "GLOBAL" && $5 != "WEAK") next
        ndx = $7
        if (ndx ~ /^[0-9]+$/) ndx = secname[ndx + 0]
        sym[$8] = sprintf("symbol %s %s %s %s", $8, hex($2), $5, ndx)
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
        k = 0
        for (s in sym) names[++k] = s
        for (i = 2; i <= k; i++) {
            t = names[i]
            for (j = i - 1; j >= 1 && names[j] > t; j--) names[j + 1] = names[j]
            names[j + 1] = t
        }
        for (i = 1; i <= k; i++) print sym[names[i]]
        # 節の中身は引数の順ではなく名前の順に出す
        k = 0
        for (s in xout) xn[++k] = s
        for (i = 2; i <= k; i++) {
            t = xn[i]
            for (j = i - 1; j >= 1 && xn[j] > t; j--) xn[j + 1] = xn[j]
            xn[j + 1] = t
        }
        for (i = 1; i <= k; i++) print "bytes " xn[i] xout[xn[i]]
    }'
