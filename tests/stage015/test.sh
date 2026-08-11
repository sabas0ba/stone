#!/bin/bash
# Stage 15 テスト: 64 bit 整数の土台 (docs/stage015-tcc.md 6 章)。
#
# probe/ の各ソースを pp -> cc15a -> ld に通し，結果を ledger.txt と
# 突き合わせる。表と実測が食い違えば失敗する (Stage 14 と同じ枠組み)。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s15

cc=tmp/build/cc15e.bin
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage015/probe
hdr=stage013/libc/include/stdarg.h

ensure_build stage015

probe() {
    n=$1
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s15/$n.i" 2> /dev/null || {
        echo "ppfail"
        return
    }
    sh tools/env.sh qemu "$cc" < "tmp/s15/$n.i" > "tmp/s15/$n.o" 2> /dev/null
    ccrc=$?
    if [ "$ccrc" -ne 0 ]; then
        echo "gap $ccrc"
        return
    fi
    # 実行時支援を必ず並べる。64 bit の除算はこれを呼ぶ
    { cat "tmp/s15/$n.o" tmp/build/rt64.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > "tmp/s15/$n.bin" 2> /dev/null || {
        echo "linkfail"
        return
    }
    out=$(sh tools/env.sh qemu "tmp/s15/$n.bin" < /dev/null 2> /dev/null)
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "runfail $rc"
        return
    fi
    printf 'ok %s\n' "$(printf '%s\n' "$out" | sed -e 's/\\/\\\\/g' -e 's/\t/\\t/g' | tr '\n' '@' | sed 's/@/\\n/g')"
}

section "ビルド再現と固定点"

ok=0
for pair in cc15a.bin:stage015/cc15a.md cc15b.bin:stage015/cc15b.md \
        cc15c.bin:stage015/cc15c.md cc15d.bin:stage015/cc15d.md \
        cc15e.bin:stage015/cc15e.md; do
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "${pair##*:}" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/${pair%%:*}"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc15a..cc15e の SHA-256 が各 .md 記載値と一致"

{ cat stage015/cc15e.sc; printf '\004'; } \
    | sh tools/env.sh qemu tmp/build/cc15e.bin > tmp/s15/b3.o \
    && { cat tmp/s15/b3.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/b3.bin \
    && cmp -s tmp/s15/b3.bin tmp/build/cc15e.bin
report $? "fixpoint: cc15e が自分自身を再生成する (B2 == B3)"

# 64 bit を足しただけで，32 bit のコード生成は変えていない
ok=0
for n in sh ed mk; do
    sh tools/bundle.sh stage013/libc/include/*.h "stage013/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s15/r_$n.i" 2> /dev/null \
        && sh tools/env.sh qemu "$cc" < "tmp/s15/r_$n.i" > "tmp/s15/r_$n.o" 2> /dev/null \
        && cmp -s "tmp/s15/r_$n.o" "tmp/build/${n}13.o" || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: cc15e が既存のソース (sh / ed / mk) を cc10l と同じ .o にする"

section "適合台帳の照合 (64 bit の土台)"

nok=0
ngap=0
nbad=0
while read -r name state want rest; do
    case "$name" in ''|'#'*) continue ;; esac
    got=$(probe "$name")
    gs=${got%% *}
    gv=${got#* }
    if [ "$state" = gap ]; then
        [ "$gs" = gap ] && [ "$gv" = "$want" ]
        r=$?
        report $r "gap: $name (cc が $want で拒む) ${rest:-}"
        ngap=$((ngap + 1))
    else
        [ "$gs" = ok ] && [ "$gv" = "$want" ]
        r=$?
        if [ "$state" = bad ]; then
            report $r "bad: $name (通るが誤り) ${rest:-}"
            nbad=$((nbad + 1))
        else
            report $r "ok:  $name"
            nok=$((nok + 1))
        fi
    fi
done < tests/stage015/ledger.txt

echo
echo "   台帳: 通る $nok 件 / 未対応 $ngap 件 / 通るが誤り $nbad 件"

# ---------------------------------------------------------------------------
section "第 5 部 その 1: tcc に RV32 の対象を足す (docs/stage015-riscv32.md)"

# 素材とホストの道具があるときだけ走る。ここで作るのは**ホストの gcc が
# 作った tcc** であり，ブートストラップ鎖の一部ではない (tools/tcc.sh の
# 頭の注意書き)。素材の無い環境 (CI) では飛ばす
tccdir=docs/external/tcc
if [ ! -d "$tccdir" ]; then
    echo "   skip: $tccdir が無い (sh tools/fetch.sh tcc で取得できる)"
elif ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "   skip: ホストに gcc / make が無い"
elif ! command -v patch >/dev/null 2>&1; then
    echo "   skip: ホストに patch が無い"
else
    # RV64 を壊していないこと。riscv32.patch は RV32 と RV64 で共通の
    # ファイル (riscv64-gen.c) を触るので，ここは毎回見る必要がある
    sh tools/tcc.sh riscv64 > tmp/s15/tcc-riscv64.log 2>&1
    report $? "tcc: 上流の RV64 対象が patch 適用後もビルドできる"

    sh tools/tcc.sh riscv32 > tmp/s15/tcc-riscv32.log 2>&1
    report $? "tcc: RV32 対象の交差コンパイラ (riscv32-tcc) がビルドできる"

    if [ -x tmp/tcc/build/riscv32-tcc ]; then
        printf 'int add(int a, int b) { return a + b; }\n' > tmp/s15/tccadd.c
        tmp/tcc/build/riscv32-tcc -c tmp/s15/tccadd.c -o tmp/s15/tccadd.o \
            > /dev/null 2>&1
        report $? "tcc: riscv32-tcc が C をオブジェクトへ通す"

        # ELF の見出しだけを見る。ELFCLASS32 (5 バイト目 = 1) と
        # e_machine = EM_RISCV (243) であること
        cls=$(od -An -tu1 -j 4 -N 1 tmp/s15/tccadd.o | tr -d ' ')
        mach=$(od -An -tu2 -j 18 -N 2 tmp/s15/tccadd.o | tr -d ' ')
        [ "$cls" = 1 ] && [ "$mach" = 243 ]
        report $? "tcc: 吐いたオブジェクトが ELF32 の RISC-V である (class=$cls machine=$mach)"
    else
        report 1 "tcc: riscv32-tcc が無いので出力を見られない"
    fi
fi

summary
