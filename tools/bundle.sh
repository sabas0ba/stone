#!/bin/sh
# ソース群を pp への入力 (束ね) へ変換する。形式は docs/stage009-pp.md 2.2。
#
#   bundle := "#!stone-bundle\n" member* EOT
#   member := "@" name " " size "\n" content
#
# 最後に並べたファイルが前処理対象の翻訳単位になる。それより前のものは
# #include で参照できる (依存を先に，本体を後に並べる)。
#
# 使用法: bundle.sh util.h main.c | qemu pp.bin > main.i
set -eu

printf '#!stone-bundle\n'
for f in "$@"; do
    name=$(basename -- "$f")
    size=$(wc -c < "$f" | tr -d ' \t')
    printf '@%s %s\n' "$name" "$size"
    cat -- "$f"
done
printf '\004'
