#!/bin/bash
# Stage 14 テスト: 適合台帳の照合 (docs/stage014-external.md 4 章)。
#
# probe/ の各ソースを pp -> cc -> ld に通し，結果を ledger.txt と突き合わせる。
#
#   ok   通ってその出力になる
#   gap  cc がその終了コードで拒む (未対応。拒むこと自体は正しい振舞い)
#   bad  通ってしまうが結果が誤っている
#
# **台帳と実測が食い違えば失敗する。** 直したのに表がそのままでも，
# 壊したのに気づかなくても，等しく捕まえるためである。gap が ok に
# 変わったらそれは前進なので，表を直して commit する。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s14

cc=tmp/build/cc.bin
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage014/probe
hdr=stage013/libc/include/stdarg.h

ensure_build stage013

# probe を 1 つ通す。結果を "状態 値" の形で標準出力へ返す
#   gap <cc の終了コード> / ok <出力> / linkfail / runfail <終了コード>
probe() {
    n=$1
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/$n.i" 2> /dev/null || {
        echo "ppfail"
        return
    }
    sh tools/env.sh qemu "$cc" < "tmp/s14/$n.i" > "tmp/s14/$n.o" 2> /dev/null
    ccrc=$?
    if [ "$ccrc" -ne 0 ]; then
        echo "gap $ccrc"
        return
    fi
    { cat "tmp/s14/$n.o"; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > "tmp/s14/$n.bin" 2> /dev/null || {
        echo "linkfail"
        return
    }
    out=$(sh tools/env.sh qemu "tmp/s14/$n.bin" < /dev/null 2> /dev/null)
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "runfail $rc"
        return
    fi
    # 改行を \n として 1 行に畳む (台帳に書ける形にする)
    printf 'ok %s\n' "$(printf '%s\n' "$out" | sed -e 's/\\/\\\\/g' -e 's/\t/\\t/g' | tr '\n' '@' | sed 's/@/\\n/g')"
}

section "適合台帳の照合"

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
        # ok と bad は「通って，その出力になる」ことを見る。違いは意味づけだけ
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
done < tests/stage014/ledger.txt

echo
echo "   台帳: 通る $nok 件 / 未対応 $ngap 件 / 通るが誤り $nbad 件"

summary
