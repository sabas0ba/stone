#!/bin/bash
# Stage 18 の検査。設計は docs/stage018-ncc.md。
#
# 第 1 部 (M1) はアセンブラ nas1 である。見るのは
#
#   1. cc15as で組んだ nas1 が stone の OS (kernel29) の上で走ること
#   2. 命令の符号化が ISA の文面から書いた期待と一致すること
#      (verify 層の objdump で逆アセンブルして照合する。docs/plan.md 2.2)
#   3. 節・データ・再配置・記号が，入力から手で数えた期待と一致すること
#   4. 誤った入力を誤りとして止めること
#   5. ホストの cc で組んだ nas1 と同じ .o を出すこと (同 7 章の 5)
#   6. Linux 5.4 の .S の単位を組み立てられること
#
# 6 は Linux のソースと Kbuild の記録 (tools/linux19.sh spre) が要るので，
# 無ければ飛ばし，飛ばしたことを出す。
set -u

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
out=tmp/s18t
rm -rf "$out"
mkdir -p "$out"

ensure_build stage018

section "nas1: stone の OS の上で走る (docs/stage018-ncc.md 10 章の M1)"

# ld18 の出力は ELF である (入力の前置部が 'E')。ここを取り違えると spawn が
# ENOEXEC で落ちる (tests/stage017 と同じ検査)
[ "$(od -An -c -N 4 tmp/build/nas1 | tr -d ' ')" = '177ELF' ]
report $? "build: nas1 が OS の実行形式 (ELF) として組まれている"

# 誤った入力。1 つずつ別の単位にして，終了コードが 0 でないことを見る
mkdir -p "$out/bad"
printf ' beq a0, a1, .+4096\n' > "$out/bad/far.s"
printf ' frobnicate a0, a1\n' > "$out/bad/unknown.s"
printf ' addi a0, a0, 2048\n' > "$out/bad/imm.s"
printf ' .err\n' > "$out/bad/err.s"
printf ' lw a0, 0(q9)\n' > "$out/bad/reg.s"
printf 'x:\nx:\n' > "$out/bad/dup.s"
# 空の入力 (Kbuild の lib-ksyms は echo | $(CC) -x assembler - を組む)
printf '\n' > "$out/empty.s"

# Linux の .S の単位 (あれば)
lx=tmp/linux19/kblog-stone/asm/out
linux=()
if [ -d "$lx" ]; then
    for f in "$lx"/*.s; do
        linux+=("$f")
    done
fi
# .incbin が読むもの。中身は何でもよい (照合はホスト版とのバイト一致で行う)
mkdir -p "$out/inc/usr" "$out/inc/arch/riscv/kernel/vdso"
printf 'TRAILER!!!\n' > "$out/inc/usr/initramfs_data.cpio"
printf '\177ELFvdso' > "$out/inc/arch/riscv/kernel/vdso/vdso.so"

NAS18_INC="$out/inc" sh tools/nas18.sh run "$out/stone" \
    tests/stage018/nas1/insn.s tests/stage018/nas1/elf.s "$out/empty.s" \
    "$out"/bad/*.s "${linux[@]}" 2> "$out/run.err"
[ -s "$out/stone/insn.o" ] && [ -s "$out/stone/elf.o" ]
report $? "run: kernel29 の上で nas1 が試験の入力を組み立てる"
[ -s "$out/stone/empty.o" ]
report $? "run: 空の入力から .o を出す (Kbuild の lib-ksyms の形)"

section "nas1: 命令の符号化 (verify 層の objdump と照合)"

sh verify/nas1insn.sh "$out/stone/insn.o" tests/stage018/nas1/insn.s > "$out/insn.log" 2>&1
r=$?
tail -n 1 "$out/insn.log"
report $r "insn: RV32IMA・Zicsr・Zifencei と疑似命令が期待どおりに符号化される"

section "nas1: 節・データ・再配置・記号"

sh verify/nas1elf.sh "$out/stone/elf.o" .data .rodata.str1.1 > "$out/elf.got" 2> "$out/elf.err"
diff -u tests/stage018/nas1/elf.expect "$out/elf.got" > "$out/elf.diff"
r=$?
[ $r -eq 0 ] || head -40 "$out/elf.diff"
report $r "elf: 節の旗・大きさ，再配置 (HI20 / LO12 / PCREL / CALL_PLT / BRANCH / JAL / 32 / ADD・SUB)，記号が期待どおり"

section "nas1: 誤った入力を止める"

for b in far unknown imm err reg dup; do
    [ ! -f "$out/stone/$b.o" ] && grep -q "stone 版が失敗した: .*/$b.s" "$out/run.err"
    report $? "bad: $b.s を誤りとして止める"
done

section "nas1: ホストの cc で組んだものと同じ .o を出す (docs/stage018-ncc.md 7 章の 5)"

if command -v "${HOSTCC:-gcc}" > /dev/null 2>&1; then
    sh tools/nas18.sh host > /dev/null 2>&1
    report $? "host: ホストの cc で -Werror のまま組める"
    for f in tests/stage018/nas1/insn.s tests/stage018/nas1/elf.s "${linux[@]}"; do
        b=$(basename "$f" .s)
        tmp/nas1/host/nas1 -I "$out/inc" -o "$out/host-$b.o" "$f" 2> /dev/null \
            && cmp -s "$out/host-$b.o" "$out/stone/$b.o"
        report $? "same: $b"
    done
else
    echo "skip ホストの cc が無い (HOSTCC)"
fi

section "nas1: Linux 5.4 の .S の単位"

if [ ${#linux[@]} -gt 0 ]; then
    n=0
    for f in "${linux[@]}"; do
        b=$(basename "$f" .s)
        [ -s "$out/stone/$b.o" ] && n=$((n + 1))
    done
    echo "   ${#linux[@]} 単位中 $n 単位を組み立てた"
    [ "$n" -eq ${#linux[@]} ]
    report $? "linux: rv32-stone の .S の単位をすべて組み立てる"
else
    echo "skip Linux の .S の単位が無い (sh tools/fetch.sh linux54，sh tools/kbuildlog.sh run，sh tools/linux19.sh spre)。**この節は検査していない**"
fi

summary
