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
#  13. (M4) Linux 5.4 の C の単位をすべて ncc1 で訳し，nas1 で組み立てること
#  14. (M5) nar1 の書庫・nld1 の -r / -shared / --just-symbols / --build-id・
#      nobj1 の nm / objcopy / objdump が期待どおりで，ホスト版と一致すること
#  15. (M5) Kbuild を我々の道具で回して vmlinux ができること
#  16. (M6) そのカーネルが我々の SBI の上で起動し，initramfs の init (ncc1 で
#      組んだもの) が syscall を確かめて電源を切ること
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
    tests/stage018/nld1/lds2.s tests/stage018/nld1/cw1.s tests/stage018/nld1/cw2.s \
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
for b in start io main other lds lds2 cw1 cw2 ld_undef ld_dup ld_far ld_lo; do
    [ -f "$out/stone/$b.o" ] && cp "$out/stone/$b.o" "$ld/root/"
done
cp tests/stage018/nld1/lds.lds tests/stage018/nld1/lds2.lds "$ld/root/"
printf 'SECTIONS { .text : { *(.text) } ASSERT(0, "boom") }\n' > "$ld/root/assert.lds"
cat > "$ld/root/go.sh" <<'GO'
nld1 -o t.elf start.o io.o main.o other.o
echo "rc t $?"
nld1 -T lds.lds -o lds.elf lds.o
echo "rc lds $?"
nld1 -z noexecstack -m elf32lriscv -T lds2.lds -o lds2.elf lds2.o
echo "rc lds2 $?"
nld1 -o cwa.elf cw1.o cw2.o
echo "rc cwa $?"
nld1 -o cwb.elf cw2.o cw1.o
echo "rc cwb $?"
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
grep -q '^rc lds2 0$' "$ld/out.console" \
    && sh verify/nld1elf.sh "$ld/out/lds2.elf" .data > "$out/lds2.got" 2> /dev/null \
    && diff -u tests/stage018/nld1/lds2.expect "$out/lds2.got"
report $? "lds2: 5 段の前方の参照が収束し，SECTIONS 直下の複合代入をすべて受け，値 0 の入口を保つ (-z の引数も読み飛ばす)"
for e in cwa cwb; do
    grep -q "^rc $e 0\$" "$ld/out.console" \
        && sh verify/nld1elf.sh "$ld/out/$e.elf" 2> /dev/null | grep -q '^symbol cw 0x[0-9a-f]* GLOBAL \.bss$'
    report $? "common: COMMON が weak の定義に勝つ ($e: 結合の順を替えても同じ)"
done

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
    asm) what="inline asm の被演算子 (64 bit の値・記憶域・jump 擬似命令) を手で書いた期待と比べる" ;;
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

section "ncc1: Linux 5.4 の C の単位 (docs/stage018-ncc.md 10 章の M4)"

# 前処理した .i は 404 MB あり，stone の OS のファイルシステムに入らない。
# ホストの cc で組んだ ncc1 と nas1 で通す (同じ .s を出すことは上の節で見る)
kbtree=tmp/kblog-stone
if [ -d tmp/linux19/kblog-stone/out ] && [ -x tmp/nas1/host/ncc1 ] && [ -d "$kbtree" ]; then
    sh tools/linux19.sh ncc "$kbtree" > "$out/linux-ncc.log" 2>&1
    sed -n 's/^ncc: /   /p' "$out/linux-ncc.log"
    n=$(find tmp/linux19/kblog-stone/out -name '*.i' | wc -l | tr -d ' ')
    ok=$(grep -lx 0 tmp/linux19/kblog-stone/ncc/*.rc | wc -l | tr -d ' ')
    [ "$n" -gt 0 ] && [ "$ok" -eq "$n" ]
    report $? "linux: rv32-stone の C の単位をすべて ncc1 で訳し，nas1 で組み立てる"
else
    echo "skip Linux の C の単位が無い (sh tools/fetch.sh linux54，sh tools/kbuildlog.sh run，sh tools/linux19.sh pre)。**この節は検査していない**"
fi

section "nar1・nobj1・nld1: 書庫・-r・-shared・周辺の道具 (docs/stage018-ncc.md 10 章の M5)"

for p in nar1 nobj1; do
    [ "$(od -An -c -N 4 tmp/build/$p | tr -d ' ')" = '177ELF' ]
    report $? "build: $p が OS の実行形式 (ELF) として組まれている"
done

# 入力は上の節で stone の ncc1・nas1 が組んだ .o。1 回の起動でまとめて行う
m5="$out/m5"
rm -rf "$m5"
mkdir -p "$m5/root"
cp tmp/build/nar1 tmp/build/nobj1 tmp/build/nld1 tmp/build/nas1 "$m5/root/"
for b in start libc gnu asm; do
    cp "$cc/out/$b.o" "$m5/root/"
done
# 16 バイトを超える名前のメンバ (長い名前の表を使う)
cp "$cc/out/libc.o" "$m5/root/test_libc_with_a_long_name.o"
cp tests/stage018/nld1/dso.s tests/stage018/nld1/dso.lds "$m5/root/"
cat > "$m5/root/go.sh" <<'GO'
nar1 rcs lib.a libc.o
echo "rc ar $?"
nld1 -o ar.elf start.o asm.o lib.a
echo "rc arlink $?"
nar1 cDPrST inner.a test_libc_with_a_long_name.o
echo "rc thin1 $?"
nar1 cDPrsT outer.a inner.a gnu.o
echo "rc thin2 $?"
nld1 -o thin.elf start.o --whole-archive outer.a --no-whole-archive
echo "rc thinlink $?"
nld1 -r -o r.o libc.o gnu.o
echo "rc r $?"
nld1 -o r.elf start.o r.o
echo "rc rlink $?"
nas1 -o dso.o dso.s
nld1 -shared -s -soname=libtest.so.1 --build-id --hash-style=both -T dso.lds -o dso.so dso.o
echo "rc shared $?"
nld1 -T dso.lds -o dsoexec.elf dso.o
echo "rc dsoexec $?"
nobj1 objcopy -G dso_alpha -G dso_beta dsoexec.elf dsodummy.o
echo "rc objcopyG $?"
nld1 -r --just-symbols dsodummy.o -o dsosyms.o
echo "rc justsym $?"
nobj1 objcopy -S dsoexec.elf dsostrip.elf
echo "rc objcopyS $?"
nobj1 objcopy -O binary r.elf r.bin
echo "rc objcopyO $?"
nobj1 nm -n r.elf > nm.txt
echo "rc nm $?"
nobj1 objdump -h lib.a > objdump.txt
echo "rc objdump $?"
GO
sh tools/os18.sh run "$m5/root" "$m5/out"
for s in ar arlink thin1 thin2 thinlink r rlink shared dsoexec objcopyG justsym objcopyS objcopyO nm objdump; do
    grep -q "^rc $s 0\$" "$m5/out.console" || echo "   失敗: $s"
done

runcmp() {
    # runcmp <実行形式> <期待>
    sh tools/env.sh qemu "$1" < /dev/null > "$1.out" 2>&1
    echo "rc $?" >> "$1.out"
    diff -u "$2" "$1.out" > "$1.diff"
}
runcmp "$m5/out/ar.elf" tests/stage018/c/asm.expect
report $? "ar: 書庫 (記号表付き) から参照されるメンバだけを引き込んで結合し，走る"
runcmp "$m5/out/thin.elf" tests/stage018/c/gnu.expect
report $? "thin: thin の書庫を入れ子にし (長い名前のメンバ)，--whole-archive で結合して走る"
runcmp "$m5/out/r.elf" tests/stage018/c/gnu.expect
report $? "-r: 2 つの .o を再配置可能なまま 1 つにまとめ，それを結合して走る"
# thin の書庫のメンバは書庫の場所からの経路で出る
sh tools/env.sh run riscv64-unknown-elf-ar t "$m5/out/outer.a" 2> /dev/null | sed 's|.*/||' > "$m5/ar-t.got" \
    && printf 'test_libc_with_a_long_name.o\ngnu.o\n' | diff - "$m5/ar-t.got" > /dev/null \
    && sh tools/env.sh run riscv64-unknown-elf-nm -s "$m5/out/outer.a" 2> /dev/null \
        | grep -q '^main in .*gnu.o$'
report $? "ar: verify 層の ar・nm が thin の書庫と記号表を読む (入れ子を展開している)"
sh verify/nld1dyn.sh "$m5/out/dso.so" > "$m5/dso.got" 2> /dev/null \
    && diff -u tests/stage018/nld1/dso.expect "$m5/dso.got"
report $? "-shared: PHDRS・FILEHDR・VERSION・.hash・.gnu.hash・.dynsym・.gnu.version_d・.dynamic が期待どおり"
# build-id は .note の中身を 0 にしたファイル全体の SHA-1 (ホストの sha1sum で照合)
bid=$(sh tools/env.sh run riscv64-unknown-elf-readelf -n "$m5/out/dso.so" 2> /dev/null \
    | sed -n 's/.*Build ID: \([0-9a-f]*\).*/\1/p')
noff=$(sh tools/env.sh run riscv64-unknown-elf-readelf -S -W "$m5/out/dso.so" 2> /dev/null \
    | sed -n 's/.*] \.note  *NOTE  *[0-9a-f]*  *\([0-9a-f]*\) .*/\1/p')
cp "$m5/out/dso.so" "$m5/dso.zero"
dd if=/dev/zero of="$m5/dso.zero" bs=1 seek=$((0x$noff + 16)) count=20 conv=notrunc 2> /dev/null
[ -n "$bid" ] && [ "$bid" = "$(sha1sum "$m5/dso.zero" | cut -c1-40)" ]
report $? "--build-id: .note.gnu.build-id が出力の SHA-1 と一致する"
sh tools/env.sh run riscv64-unknown-elf-readelf -s -W "$m5/out/dsosyms.o" 2> /dev/null \
    | awk '$8 == "dso_alpha" || $8 == "dso_beta" || $8 == "dso_gamma" { print $8, $2, $7 }' \
    | sort > "$m5/justsym.got"
printf 'dso_alpha 00000800 ABS\ndso_beta 00000808 ABS\n' | diff - "$m5/justsym.got" > /dev/null
report $? "--just-symbols: objcopy -G で残した大域の記号だけが絶対値の記号になる (vdso-syms.o の形)"
sh tools/env.sh run riscv64-unknown-elf-readelf -S -W "$m5/out/dsostrip.elf" 2> /dev/null \
    | grep -q '\.symtab'
[ $? -ne 0 ] && sh tools/env.sh run riscv64-unknown-elf-readelf -S -W "$m5/out/dsostrip.elf" 2> /dev/null \
    | grep -q ' \.text '
report $? "objcopy -S: 記号表を除き，割り付ける節を残す"
sh tools/env.sh run riscv64-unknown-elf-objcopy -O binary "$m5/out/r.elf" "$m5/r.bin.gnu" 2> /dev/null \
    && cmp -s "$m5/r.bin.gnu" "$m5/out/r.bin"
report $? "objcopy -O binary: verify 層の objcopy と同じバイト列"
sh tools/env.sh run riscv64-unknown-elf-nm -n "$m5/out/r.elf" > "$m5/nm.gnu" 2> /dev/null \
    && diff -u "$m5/nm.gnu" "$m5/out/nm.txt" > /dev/null
report $? "nm -n: verify 層の nm と同じ出力 (System.map の材料)"
sh tools/env.sh run riscv64-unknown-elf-objdump -h "$m5/out/lib.a" 2> /dev/null \
    | sed "s|$m5/out/||" > "$m5/objdump.gnu" \
    && diff -u "$m5/objdump.gnu" "$m5/out/objdump.txt" > /dev/null
report $? "objdump -h: verify 層の objdump と同じ出力 (lib-ksyms の台本の材料)"
if [ -x tmp/nas1/host/nld1 ] && [ -x tmp/nas1/host/nar1 ]; then
    (
        cd "$m5/out" && "$repo_root/tmp/nas1/host/nar1" cDPrsT host-outer.a inner.a gnu.o 2> /dev/null
    )
    cmp -s "$m5/out/host-outer.a" "$m5/out/outer.a"
    report $? "same: nar1 の thin の書庫 (ホストの cc で組んだものと同じ)"
    tmp/nas1/host/nld1 -shared -s -soname=libtest.so.1 --build-id --hash-style=both \
        -T tests/stage018/nld1/dso.lds -o "$m5/host-dso.so" "$m5/out/dso.o" \
        && cmp -s "$m5/host-dso.so" "$m5/out/dso.so"
    report $? "same: nld1 -shared の出力 (同)"
    tmp/nas1/host/nld1 -r -o "$m5/host-r.o" "$cc/out/libc.o" "$cc/out/gnu.o" \
        && cmp -s "$m5/host-r.o" "$m5/out/r.o"
    report $? "same: nld1 -r の出力 (同)"
else
    echo "skip ホスト版が無い (ホストの cc が無い)"
fi

section "Kbuild: 我々の道具で vmlinux を組む (docs/stage018-ncc.md 10 章の M5)"

# ホストの make で Kbuild を回し，gcc・ld・ar・nm・objcopy・objdump を我々の
# ものに差し替える (tools/kb18.sh)。Linux のソースと上流の conf が要る
if [ -d docs/external/linux54/arch/riscv ] && [ -x tmp/kconfref/flex/src/flex ] \
    && [ -x tmp/nas1/host/ncc1 ]; then
    kbt=tmp/kb18-stone
    rm -rf "$kbt"
    sh tools/kb18.sh run "$kbt" allnoconfig KCONFIG_ALLCONFIG="$repo_root/stage019/rv32-stone.config" \
        > "$out/kb18.log" 2>&1
    report $? "kbuild: allnoconfig + rv32-stone.config で vmlinux・vdso.so・System.map までできる"
    [ -s "$kbt/System.map" ] && grep -q ' T start_kernel$' "$kbt/System.map"
    report $? "kbuild: System.map (nobj1 nm -n) に start_kernel がある"
    sh tools/env.sh run riscv64-unknown-elf-readelf -h "$kbt/vmlinux" 2> /dev/null \
        | grep -q 'Entry point address: *0xc0000000'
    report $? "kbuild: vmlinux の入口が _start (0xc0000000)"
else
    echo "skip Linux のソースか conf が無い (sh tools/fetch.sh linux54，sh tools/kconfref.sh flex)。**この節は検査していない**"
fi

section "起動: 我々の SBI の上で Linux が initramfs の init まで走る (docs/stage018-ncc.md 10 章の M6)"

# SBI と init はホストの cc で組んだ道具で組む (tools/boot18.sh)。stone の
# ncc1 が同じ .s を出すことをここで確かめる (前処理はホストの cpp)
if [ -s tmp/kb18-stone/arch/riscv/boot/Image ] && [ -x tmp/kb18-stone/usr/gen_init_cpio ]; then
    sh tools/boot18.sh build tmp/kb18-stone > "$out/boot18-build.log" 2>&1
    report $? "build: SBI (sbi1)・init (init1)・initramfs を組む"
    bt="$out/boot"
    rm -rf "$bt"
    mkdir -p "$bt/root"
    cp tmp/build/ncc1 "$bt/root/"
    cp tmp/boot18/sbi1.o.i "$bt/root/sbi1.i"
    cp tmp/boot18/init1.o.i "$bt/root/init1.i"
    printf 'ncc1 -o sbi1.s sbi1.i\necho "rc sbi1 $?"\nncc1 -o init1.s init1.i\necho "rc init1 $?"\n' \
        > "$bt/root/go.sh"
    sh tools/os18.sh run "$bt/root" "$bt/out"
    cmp -s "$bt/out/sbi1.s" tmp/boot18/sbi1.o.s && cmp -s "$bt/out/init1.s" tmp/boot18/init1.o.s
    report $? "same: stone の ncc1 が SBI と init の .s をホスト版と同じに出す"
    sh tools/boot18.sh run tmp/kb18-stone > "$out/boot18-run.log" 2>&1
    r=$?
    report $r "boot: QEMU が終了コード 0 で止まる (init の reboot(POWER_OFF) → SBI の shutdown)"
    con=tmp/boot18/console.log
    grep -q '^Linux version 5\.4\.302' "$con" && grep -q '^Run /init as init process' "$con"
    report $? "boot: カーネルが起動し，initramfs の /init を走らせる"
    [ "$(grep -c '^init: ok ' "$con")" -eq 8 ] && ! grep -q '^init: FAIL' "$con" \
        && grep -q '^stone sbi: shutdown' "$con"
    report $? "boot: init の syscall の確かめ (write・getpid・uname・openat・read・close) がすべて通る"
else
    echo "skip Image が無い (上の Kbuild の節が要る)。**この節は検査していない**"
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
