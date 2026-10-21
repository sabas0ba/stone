#!/bin/bash
# Stage 18 の検査。設計は docs/stage018-ncc.md。
#
# 第 1 部 (M1) はアセンブラ nas1，第 2 部 (M2) はリンカ nld1 と素の実行環境
# である。見るのは
#
#   1. cc15as で組んだ nas1 が stone の OS (kernel29) の上で走ること
#   2. 命令の符号化が ISA の文面から書いた期待と一致すること
#      (verify 層の objdump で逆アセンブルして照合する。docs/plan.md 2.2)
#   3. 節・データ・再配置・記号が，入力から手で数えた期待と一致すること
#   4. 誤った入力を誤りとして止めること
#   5. ホストの cc で組んだ nas1 と同じ .o を出すこと (同 7 章の 5)
#   6. Linux 5.4 の .S の単位を組み立てられること
#   7. stone の OS の上で nld1 が結合した実行形式が，QEMU の上で素のまま走り，
#      別の .o の記号への再配置がすべて正しく当たっていること
#   8. リンカ台本の割付けが，台本から手で数えた期待と一致すること
#   9. nld1 も，誤りを止め，ホストの cc で組んだものと同じ出力を出すこと
#  10. (M3) stone の OS の上で ncc1 が C を訳し，nas1 と nld1 で組んだものが
#      QEMU の上でホストの gcc と同じ出力を出すこと (tests/stage018/c)
#  11. psABI の文面から手で書いたアセンブリと C が呼び合えること (abi.s)
#  12. ncc1 も，誤りを止め，ホストの cc で組んだものと同じ .s を出すこと
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

# nld1 の誤った入力 (第 2 部)。どれも nas1 は通り，結合で止まるべきもの
mkdir -p "$out/ldbad"
printf ' .globl _start\n_start:\n call nowhere\n' > "$out/ldbad/ld_undef.s"
printf ' .globl _start\n_start:\n .globl twice\ntwice:\n ret\n' > "$out/ldbad/ld_dup.s"
printf ' .globl _start\n_start:\n beq zero, zero, faraway\n .bss\n .zero 8192\nfaraway:\n' > "$out/ldbad/ld_far.s"
printf ' .globl _start\n_start:\nlab:\n addi a0, a0, %%pcrel_lo(lab)\n' > "$out/ldbad/ld_lo.s"

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
    tests/stage018/rt/start.s tests/stage018/rt/io.s \
    tests/stage018/nld1/main.s tests/stage018/nld1/other.s tests/stage018/nld1/lds.s \
    "$out"/ldbad/*.s "$out"/bad/*.s "${linux[@]}" 2> "$out/run.err"
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

section "nld1: stone の OS の上で結合する (docs/stage018-ncc.md 10 章の M2)"

[ "$(od -An -c -N 4 tmp/build/nld1 | tr -d ' ')" = '177ELF' ]
report $? "build: nld1 が OS の実行形式 (ELF) として組まれている"

# 結合は 1 回の起動でまとめて行う。入力は上で stone の nas1 が組んだ .o
ld="$out/ld"
rm -rf "$ld"
mkdir -p "$ld/root"
cp tmp/build/nld1 "$ld/root/nld1"
for b in start io main other lds ld_undef ld_dup ld_far ld_lo; do
    [ -f "$out/stone/$b.o" ] && cp "$out/stone/$b.o" "$ld/root/"
done
cp tests/stage018/nld1/lds.lds "$ld/root/"
printf 'SECTIONS { .text : { *(.text) } ASSERT(0, "boom") }\n' > "$ld/root/assert.lds"
cat > "$ld/root/go.sh" <<'GO'
nld1 -o t.elf start.o io.o main.o other.o
echo "rc t $?"
nld1 -T lds.lds -o lds.elf lds.o
echo "rc lds $?"
nld1 -o undef.elf ld_undef.o
echo "rc undef $?"
nld1 -o dup.elf ld_dup.o other.o
echo "rc dup $?"
nld1 -o far.elf ld_far.o
echo "rc far $?"
nld1 -o lo.elf ld_lo.o
echo "rc lo $?"
nld1 -T assert.lds -o assert.elf lds.o
echo "rc assert $?"
GO
sh tools/os18.sh run "$ld/root" "$ld/out"
grep -q '^rc t 0$' "$ld/out.console" && [ -s "$ld/out/t.elf" ]
report $? "link: 起動・出力・本体の 4 つの .o を既定の台本で結合する"
grep -q '^rc lds 0$' "$ld/out.console" && [ -s "$ld/out/lds.elf" ]
report $? "link: リンカ台本 (-T) で結合する"

section "nld1: 素の実行環境で走る (別の .o の記号への再配置)"

sh tools/env.sh qemu "$ld/out/t.elf" < /dev/null > "$out/t.out" 2>&1
r=$?
diff -u tests/stage018/nld1/main.expect "$out/t.out" > "$out/t.diff"
d=$?
[ $d -eq 0 ] || head -30 "$out/t.diff"
[ $r -eq 0 ] && [ $d -eq 0 ]
report $? "run: CALL_PLT・PCREL・HI20/LO12・JAL・BRANCH・32・ADD/SUB・weak・COMMON・台本の記号がすべて正しい (終了コード 0)"

section "nld1: リンカ台本"

sh verify/nld1elf.sh "$ld/out/lds.elf" .data .rodata > "$out/lds.got" 2> "$out/lds.err"
diff -u tests/stage018/nld1/lds.expect "$out/lds.got" > "$out/lds.diff"
r=$?
[ $r -eq 0 ] || head -40 "$out/lds.diff"
report $r "lds: AT・ALIGN・SORT・KEEP・/DISCARD/・PROVIDE・BYTE/SHORT/LONG・ADDR/SIZEOF/LOADADDR・orphan の置き場所が期待どおり"

section "nld1: 誤った入力を止める"

for b in undef dup far lo assert; do
    grep -q "^rc $b [1-9]" "$ld/out.console" && [ ! -f "$ld/out/$b.elf" ]
    report $? "bad: $b を誤りとして止める"
done

section "nld1: ホストの cc で組んだものと同じ出力を出す"

if [ -x tmp/nas1/host/nld1 ]; then
    tmp/nas1/host/nld1 -o "$out/host-t.elf" "$out"/stone/start.o "$out"/stone/io.o \
        "$out"/stone/main.o "$out"/stone/other.o && cmp -s "$out/host-t.elf" "$ld/out/t.elf"
    report $? "same: t.elf"
    tmp/nas1/host/nld1 -T tests/stage018/nld1/lds.lds -o "$out/host-lds.elf" \
        "$out"/stone/lds.o && cmp -s "$out/host-lds.elf" "$ld/out/lds.elf"
    report $? "same: lds.elf"
else
    echo "skip ホスト版が無い (ホストの cc が無い)"
fi

section "ncc1: stone の OS の上で C を訳して組む (docs/stage018-ncc.md 10 章の M3)"

[ "$(od -An -c -N 4 tmp/build/ncc1 | tr -d ' ')" = '177ELF' ]
report $? "build: ncc1 が OS の実行形式 (ELF) として組まれている"

cc="$out/cc"
rm -rf "$cc"
mkdir -p "$cc/root"
cp tmp/build/ncc1 tmp/build/nas1 tmp/build/nld1 "$cc/root/"
cp tests/stage018/rt/start.s tests/stage018/rt/libc.c "$cc/root/"
ctests=""
for f in tests/stage018/c/*.c; do
    ctests="$ctests $(basename "$f" .c)"
    cp "$f" "$cc/root/"
done
cp tests/stage018/c/abi.s "$cc/root/abi_asm.s"
# 誤った入力 (どれも ncc1 が止めるべきもの)
printf 'int main(void) { return 0 }\n' > "$cc/root/bad_syntax.c"
printf 'int main(void) { return undeclared_name; }\n' > "$cc/root/bad_undecl.c"
printf 'struct s { int x; };\nint main(void) { struct s v; int i = v; return i; }\n' > "$cc/root/bad_type.c"
printf 'static void f(void);\nint main(void) { f(); return 0; }\n' > "$cc/root/bad_static.c"
printf 'int main(void) { float f = 1.5; return (int)f; }\n' > "$cc/root/bad_float.c"
{
    echo 'nas1 -o start.o start.s'
    echo 'ncc1 -o libc.s libc.c'
    echo 'echo "rc libc $?"'
    echo 'nas1 -o libc.o libc.s'
    echo 'nas1 -o abi_asm.o abi_asm.s'
    for b in $ctests; do
        extra=""
        [ "$b" = abi ] && extra=" abi_asm.o"
        echo "ncc1 -o $b.s $b.c"
        echo "echo \"rc $b.ncc \$?\""
        echo "nas1 -o $b.o $b.s"
        echo "nld1 -o $b.elf start.o libc.o $b.o$extra"
        echo "echo \"rc $b.link \$?\""
    done
    for b in bad_syntax bad_undecl bad_type bad_static bad_float; do
        echo "ncc1 -o $b.s $b.c"
        echo "echo \"rc $b \$?\""
    done
} > "$cc/root/go.sh"
sh tools/os18.sh run "$cc/root" "$cc/out"
grep -q '^rc libc 0$' "$cc/out.console"
report $? "ncc1: 試験の libc (printf・memcpy・64 bit の除算の支援) を訳す"
for b in $ctests; do
    grep -q "^rc $b.ncc 0\$" "$cc/out.console" && grep -q "^rc $b.link 0\$" "$cc/out.console"
    report $? "compile: $b.c を訳して組む"
done

section "ncc1: 素の実行環境で走り，ホストの gcc と同じ出力を出す"

for b in $ctests; do
    if sh tools/env.sh qemu "$cc/out/$b.elf" < /dev/null > "$out/c-$b.out" 2>&1; then
        echo "rc 0" >> "$out/c-$b.out"
    else
        echo "rc $?" >> "$out/c-$b.out"
    fi
    diff -u "tests/stage018/c/$b.expect" "$out/c-$b.out" > "$out/c-$b.diff"
    r=$?
    [ $r -eq 0 ] || head -20 "$out/c-$b.diff"
    case "$b" in
    abi) what="psABI の文面から手で書いたアセンブリと呼び合う (64 bit の対・a7 とスタックの分割・可変長の偶数番とスタック・構造体の値渡しと値返し・狭い整数の拡張)" ;;
    *) what="ホストの gcc と同じ出力" ;;
    esac
    report $r "run: $b ($what)"
done

section "ncc1: 誤った入力を止める"

for b in bad_syntax bad_undecl bad_type bad_static bad_float; do
    grep -q "^rc $b [1-9]" "$cc/out.console"
    report $? "bad: $b.c を誤りとして止める"
done

section "ncc1: ホストの cc で組んだものと同じ .s を出す (docs/stage018-ncc.md 7 章の 5)"

if command -v "${HOSTCC:-gcc}" > /dev/null 2>&1; then
    sh tools/ncc18.sh host > /dev/null 2>&1
    report $? "host: ホストの cc で -Werror のまま組める"
    for b in libc $ctests; do
        src="tests/stage018/c/$b.c"
        [ "$b" = libc ] && src=tests/stage018/rt/libc.c
        tmp/nas1/host/ncc1 -o "$out/host-$b.s" "$src" 2> /dev/null \
            && cmp -s "$out/host-$b.s" "$cc/out/$b.s"
        report $? "same: $b.s"
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
