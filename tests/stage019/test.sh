#!/bin/bash
# Stage 19 の検査。設計は docs/stage019-linux.md。
#
# S1 (前処理器 npp1。同 8 章) で見るのは
#
#   1. cc15as で組んだ npp1 が stone の OS の上で走ること
#   2. C の規格の例 (C99 6.10.3.5 の例 3・4・5・7) が，規格に書かれた結果になること
#   3. Linux が使う GNU の拡張 (, ## __VA_ARGS__・名前付きの可変長・_Pragma・
#      __COUNTER__・#include_next・include guard・#if の符号無しの算術) と
#      assembler-with-cpp の扱いが期待どおりであること
#   4. 依存の記録 (-MMD) が GCC と同じ形であること
#   5. Kbuild を npp1 で回した出力が，ホストの cpp で回した出力と一致すること
#      (.o・.a・vmlinux・Image・設定・依存の記録 .cmd)
#   6. stone の OS の上の npp1 が，Linux の C・.S・リンカ台本・asm-offsets で，
#      ホストの cc で組んだ npp1 と同じ .i と .d を出すこと
#
# 2〜4 の期待は手で書いた (2 は規格の文面，3・4 はホストの gcc で一度確かめた)。
# 比べる時は，空白の量に意味の無いもの (2) は空白を除き，空白の位置を見るもの
# (3・4) は行の中の空白の量だけ詰める。
# 5・6 は Linux のソースと flex (tools/kconfref.sh flex) が要るので，無ければ
# 飛ばし，飛ばしたことを出す。
set -u

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
out=tmp/s19t
rm -rf "$out"
mkdir -p "$out"
pp=tests/stage019/pp
HOSTCC=${HOSTCC:-gcc}

ensure_build stage019

section "npp1: stone の OS の上で走る (docs/stage019-linux.md 8 章)"

[ "$(od -An -c -N 4 tmp/build/npp1 | tr -d ' ')" = '177ELF' ]
report $? "build: npp1 が OS の実行形式 (ELF) として組まれている"

root="$out/pp/root"
mkdir -p "$root"
cp tmp/build/npp1 "$root/"
cp -R "$pp"/. "$root/"
{
    for t in ex3 ex4 ex5 ex7; do
        echo "npp1 -P -o $t.i $t.c"
        echo "echo \"rc $t \$?\""
    done
    echo 'npp1 -P -Iinc1 -Iinc2 -o gnu.i gnu.c'
    echo 'echo "rc gnu $?"'
    echo 'npp1 -P -o asm.i asm.S'
    echo 'echo "rc asm $?"'
    echo 'npp1 -P -isystem sys -include pre.h -MMD dep.d -MT dep.o -o dep.i dep.c'
    echo 'echo "rc dep $?"'
    # 誤り: 閉じない #if・## が字句を作らない (C)・無いファイル
    printf '#if 1\nx\n' > "$root/bad_if.c"
    printf '#define c(a, b) a ## b\nc(+, /)\n' > "$root/bad_paste.c"
    printf '#include "no_such.h"\n' > "$root/bad_inc.c"
    for b in bad_if bad_paste bad_inc; do
        echo "npp1 -P -o $b.i $b.c"
        echo "echo \"rc $b \$?\""
    done
} > "$root/go.sh"
sh tools/os18.sh run "$root" "$out/pp/out"
for t in ex3 ex4 ex5 ex7 gnu asm dep bad_if bad_paste bad_inc; do
    rc=$(sed -n "s/^rc $t //p" "$out/pp/out.console")
    eval "rc_$t=\${rc:-none}"
done
[ "$rc_ex3" = 0 ] && [ "$rc_gnu" = 0 ] && [ "$rc_asm" = 0 ] && [ "$rc_dep" = 0 ]
report $? "run: npp1 が stone の上で前処理を終える"

section "npp1: C の規格の例 (C99 6.10.3.5)"

strip_ws() { tr -d ' \t\n'; }
squeeze() { tr -s ' \t' '  ' | sed 's/^ //; s/ $//'; }

for t in ex3 ex4 ex5 ex7; do
    [ "$(eval echo "\$rc_$t")" = 0 ] \
        && [ "$(strip_ws < "$out/pp/out/$t.i")" = "$(strip_ws < "$pp/$t.expect")" ]
    case "$t" in
    ex3) what="例 3 (再走査・# と ## と空の実引数)" ;;
    ex4) what="例 4 (文字列化・#include の名前をマクロで作る)" ;;
    ex5) what="例 5 (## と配置用の字句)" ;;
    ex7) what="例 7 (__VA_ARGS__)" ;;
    esac
    report $? "std: $what"
done

section "npp1: GNU の拡張・assembler-with-cpp・依存の記録"

squeeze < "$out/pp/out/gnu.i" | diff "$pp/gnu.expect" - > "$out/gnu.diff"
report $? "gnu: , ## __VA_ARGS__・args...・_Pragma の展開・__COUNTER__・#if・include guard・#include_next・展開の境の空白"
# 行の頭の空白も GCC と同じに出るかを見るので，頭は除かない
tr -s ' \t' '  ' < "$out/pp/out/asm.i" | sed 's/ $//' | diff "$pp/asm.expect" - > "$out/asm.diff"
report $? "asm: # の注釈の行を残す・閉じない ' を通す・字句にならない ## を 2 つの字句のまま並べる"
cmp -s "$pp/dep.expect" "$out/pp/out/dep.d"
report $? "dep: -MMD の記録 (-include の順・処理系の階層を除く・書かれた名前で見分ける)"
for b in bad_if bad_paste bad_inc; do
    [ "$(eval echo "\$rc_$b")" != 0 ] && [ "$(eval echo "\$rc_$b")" != none ]
    report $? "bad: $b.c を誤りとして止める"
done

section "npp1: Kbuild をホストの cpp で回した出力と一致する (docs/stage019-linux.md 8 章)"

linux=docs/external/linux54
if [ -f "$linux/Makefile" ] && [ -x tmp/kconfref/flex/src/flex ] && command -v "$HOSTCC" > /dev/null 2>&1; then
    sh tools/ncc18.sh host > /dev/null 2>&1
    mkdir -p tmp/npp1 "$out/log"
    "$HOSTCC" -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 \
        -o tmp/npp1/npp1 stage019/npp1.c
    report $? "host: npp1 をホストの cc で組む (C89 で警告無し)"
    cfg="KCONFIG_ALLCONFIG=$repo_root/stage019/rv32-stone.config"
    rm -rf "$out/kb-ref" "$out/kb-npp"
    sh tools/kb18.sh run "$out/kb-ref" allnoconfig "$cfg" > "$out/kb-ref.log" 2>&1 &
    ref_pid=$!
    KB18_NPP="$repo_root/tmp/npp1/npp1" KB18_NPP_LOG="$repo_root/$out/log" \
        sh tools/kb18.sh run "$out/kb-npp" allnoconfig "$cfg" > "$out/kb-npp.log" 2>&1
    r1=$?
    wait "$ref_pid"
    r2=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ]
    report $? "kbuild: npp1 とホストの cpp の両方で vmlinux と Image まで組む"
    sh tools/kb18.sh cmp "$out/kb-ref" "$out/kb-npp" > "$out/kb-cmp.log" 2>&1
    r=$?
    tail -n 1 "$out/kb-cmp.log"
    report $r "kbuild: 生成物と依存の記録 (.cmd) がすべて一致する"

    section "npp1: stone の上で Linux の前処理がホスト版と一致する"

    units="mm/highmem.o kernel/exit.o arch/riscv/kernel/head.o arch/riscv/kernel/vmlinux.lds arch/riscv/kernel/asm-offsets.s"
    # shellcheck disable=SC2086
    sh tools/npp19.sh stone "$out/log" "$out/stone" $units > "$out/stone.tsv" 2> "$out/stone.err"
    for u in $units; do
        grep -q "^$u	same\$" "$out/stone.tsv"
        report $? "stone: $u の .i と .d がホストの npp1 と一致する"
    done
else
    echo "skip Linux のソースか conf が無い (sh tools/fetch.sh linux54，sh tools/kconfref.sh flex)。**この節は検査していない**"
fi

summary
