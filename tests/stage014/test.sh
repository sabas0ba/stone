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

cc=tmp/build/cc14e.bin    # 台帳は最前線の世代で測る (docs/stage014-external.md 5.3)
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage014/probe
hdr=stage013/libc/include/stdarg.h

ensure_build stage014

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

section "ビルド再現と固定点"

ok=0
for pair in cc14a.bin:stage014/cc14.md cc14b.bin:stage014/cc14b.md \
        cc14c.bin:stage014/cc14c.md cc14d.bin:stage014/cc14d.md \
        cc14e.bin:stage014/cc14e.md; do
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "${pair##*:}" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/${pair%%:*}"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc14a..cc14e の SHA-256 が各 .md 記載値と一致"

# 世代を触ったら必ず固定点を見る (docs/dev-notes.md 3.1)
{ cat stage014/cc14e.sc; printf '\004'; } \
    | sh tools/env.sh qemu tmp/build/cc14e.bin > tmp/s14/b3.o \
    && { cat tmp/s14/b3.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s14/b3.bin \
    && cmp -s tmp/s14/b3.bin tmp/build/cc14e.bin
report $? "fixpoint: cc14e が自分自身を再生成する (B2 == B3)"

# 足したのは構文と字句の幅だけで，コード生成には触れていない。
# 既存のソースを cc10l と同じオブジェクトへコンパイルできることで示す
ok=0
for n in sh ed mk; do
    sh tools/bundle.sh stage013/libc/include/*.h "stage013/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/r_$n.i" 2> /dev/null \
        && sh tools/env.sh qemu "$cc" < "tmp/s14/r_$n.i" > "tmp/s14/r_$n.o" 2> /dev/null \
        && cmp -s "tmp/s14/r_$n.o" "tmp/build/${n}13.o" || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: cc14e が既存のソース (sh / ed / mk) を cc10l と同じ .o にする"

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

# ---------------------------------------------------------------------------
section "libc 第 14 世代 (第 7 部)"

RAMSIZE=134217728
SFSOFF=67108864
IMGSIZE=4194304

# OS (kernel13) の上で走らせる。stage013 のテストと同じ道具立て
runos14() {
    sh tools/sfs.sh pack tmp/s14/root tmp/s14/fs.img "$IMGSIZE" 128 || return 1
    rm -f tmp/s14/ram
    dd if=/dev/null of=tmp/s14/ram bs=1 seek="$RAMSIZE" 2> /dev/null
    dd if=tmp/s14/fs.img of=tmp/s14/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
        conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE=tmp/s14/ram sh tools/env.sh qemu tmp/build/kernel13.bin \
        < /dev/null > "tmp/s14/$1.out" 2>&1
}

build14() {
    sh tools/bundle.sh stage014/libc/include/*.h "tests/stage014/user/$1.c" \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/$1.i" \
        && sh tools/env.sh qemu "$cc" < "tmp/s14/$1.i" > "tmp/s14/$1.o" \
        && { printf 'E'; cat "tmp/s14/$1.o" tmp/build/l14_src_string.o \
             tmp/build/l14_src_stdlib.o tmp/build/l14_posix_sys.o \
             tmp/build/l14_posix_morecore.o tmp/build/l14_posix_stdio.o \
             tmp/build/l14_posix_assert.o; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld13.bin > "tmp/s14/$1"
}

build14 lib14
report $? "build: lib14 (printf 拡張・sprintf・assert を使う)"

rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/s14/lib14 tmp/s14/root/lib14
printf 'lib14\n' > tmp/s14/root/boot
runos14 lib14
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s14/lib14.out tests/stage014/expected/lib14.txt > /dev/null
report $? "run: printf の l / 左詰め / %s の幅，sprintf，assert (成立)"

build14 abrt
report $? "build: abrt (assert の失敗)"

rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/s14/abrt tmp/s14/root/abrt
printf 'abrt\n' > tmp/s14/root/boot
runos14 abrt
rc=$?
[ "$rc" -eq 1 ] && diff -q tmp/s14/abrt.out tests/stage014/expected/abrt.txt > /dev/null
report $? "run: assert の失敗が式の文字列を出して exit(1) する"

summary
