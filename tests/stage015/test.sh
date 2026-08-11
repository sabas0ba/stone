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

cc=tmp/build/cc15a.bin
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
    { cat "tmp/s15/$n.o"; printf '\0'; } \
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

want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' stage015/cc15a.md | cut -d' ' -f2)
got=$(sha256sum tmp/build/cc15a.bin); got=${got%% *}
[ -n "$want" ] && [ "$want" = "$got" ]
report $? "build: cc15a の SHA-256 が cc15a.md 記載値と一致"

{ cat stage015/cc15a.sc; printf '\004'; } \
    | sh tools/env.sh qemu tmp/build/cc15a.bin > tmp/s15/b3.o \
    && { cat tmp/s15/b3.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/b3.bin \
    && cmp -s tmp/s15/b3.bin tmp/build/cc15a.bin
report $? "fixpoint: cc15a が自分自身を再生成する (B2 == B3)"

# 64 bit を足しただけで，32 bit のコード生成は変えていない
ok=0
for n in sh ed mk; do
    sh tools/bundle.sh stage013/libc/include/*.h "stage013/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s15/r_$n.i" 2> /dev/null \
        && sh tools/env.sh qemu "$cc" < "tmp/s15/r_$n.i" > "tmp/s15/r_$n.o" 2> /dev/null \
        && cmp -s "tmp/s15/r_$n.o" "tmp/build/${n}13.o" || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: cc15a が既存のソース (sh / ed / mk) を cc10l と同じ .o にする"

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

summary
