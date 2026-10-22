#!/bin/sh
# verify 層: nld1 -shared が出した共有オブジェクトの program header・動的な
# 節・動的な記号・版の定義を readelf で読み，比べやすい行に直して出す
# (docs/stage018-ncc.md 16 章)。読取り専用の検証であり，ビルド成果物には
# 影響しない (docs/plan.md 2.2)。
#
# 使用法: nld1dyn.sh <repo 相対パスの共有オブジェクト>
#
# 出す行:
#   type <DYN|EXEC>
#   phdr <型> offset=<16 進> vaddr=<16 進> filesz=<16 進> memsz=<16 進> flags=<R/W/E>
#   dynamic <タグ> <値>                (番地の値は節の名前に直す)
#   dynsym <名前> <型> <結合> <節の番号 | ABS | UND>
#   versym <番号> <版>
#   verdef <番号> <旗> <名前>
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
so=$1
re=riscv64-unknown-elf-readelf

sh "$repo_root/tools/env.sh" run $re -W -h -l -S -d --dyn-syms -V "$so" | awk '
    function hex(s) { sub(/^0x/, "", s); sub(/^0+/, "", s); return "0x" (s == "" ? "0" : s) }
    /^ELF Header:/ { mode = "H"; next }
    /^Section Headers:/ { mode = "S"; next }
    /^Program Headers:/ { mode = "P"; next }
    /^ Section to Segment mapping:/ { mode = ""; next }
    /^Dynamic section/ { mode = "D"; next }
    /^Symbol table .\.dynsym./ { mode = "Y"; next }
    /^Version symbols section/ { mode = "V"; next }
    /^Version definition section/ { mode = "F"; next }
    mode == "H" && /^  Type:/ { print "type " $2; next }
    mode == "S" && /^ *\[ *[0-9]+\]/ {
        line = $0
        sub(/^ *\[ *[0-9]+\] */, "", line)
        split(line, f, " +")
        addr[hex(f[3])] = f[1]
        next
    }
    mode == "P" && $1 ~ /^[A-Z_]+$/ && $2 ~ /^0x/ {
        fl = ""
        for (i = 7; i < NF; i++) fl = fl $i
        printf "phdr %s offset=%s vaddr=%s filesz=%s memsz=%s flags=%s\n", $1, hex($2), hex($3), hex($5), hex($6), fl
        next
    }
    mode == "D" && $1 ~ /^0x/ {
        tag = $2
        gsub(/[()]/, "", tag)
        v = $3
        if (tag == "SONAME") { v = $NF; gsub(/[\[\]]/, "", v) }
        else if (tag ~ /^(HASH|GNU_HASH|STRTAB|SYMTAB|VERSYM|VERDEF)$/ && (hex(v) in addr)) v = addr[hex(v)]
        else if (v ~ /^0x/) v = hex(v)
        printf "dynamic %s %s\n", tag, v
        next
    }
    mode == "Y" && /^ +[0-9]+: / {
        name = $8
        printf "dynsym %s %s %s %s\n", (name == "" ? "-" : name), $4, $5, $7
        next
    }
    mode == "V" && /^  [0-9a-f]+:/ {
        for (i = 2; i <= NF; i += 2) {
            v = $(i + 1)
            gsub(/[()]/, "", v)
            printf "versym %s %s\n", $i, v
        }
        next
    }
    mode == "F" && /Rev:/ {
        printf "verdef %s %s %s\n", $7, $5, $NF
        next
    }'
