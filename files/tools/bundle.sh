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
#
# 名前は basename が既定だが，"名前=パス" の形で明示できる。
# #include <sys/time.h> のように斜線を含む名前はこの形でしか作れない。
set -eu

printf '#!stone-bundle\n'
for f in "$@"; do
    case $f in
    *=*) name=${f%%=*}; f=${f#*=} ;;
    *)   name=$(basename -- "$f") ;;
    esac
    size=$(wc -c < "$f" | tr -d ' \t')
    printf '@%s %s\n' "$name" "$size"
    cat -- "$f"
done
printf '\004'
