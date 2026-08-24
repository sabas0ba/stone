#!/bin/sh
# tcc の翻訳単位を**我々の OS の上で**1 本ずつ訳す (第 3 部の 3 の 2)。
#
#   tcc17.sh root          作業用の像の元 (tmp/s17/root) を組む
#   tcc17.sh unit <名前>   翻訳単位を 1 本訳し，.o を tmp/s17/obj へ出す
#   tcc17.sh all           11 本すべて (既にできているものは飛ばす)
#   tcc17.sh clean         tmp/s17 を消す
#
# ---- なぜ 1 本ずつ別の起動にするか ----
#
# 1 本あたり数分かかる。11 本を 1 度の起動でやると，**途中で殺された
# ときに全部やり直しになる** (docs/dev-notes.md 1.5)。1 本ごとに
# 起動を分け，出来た .o をホスト側へ取り出してスタンプを置く。
# 殺されても失うのは高々 1 本である。
#
# ---- 素材 ----
#
# docs/external/tcc (tools/fetch.sh tcc) と，そこへ patch を当てた
# tmp/tcc/src (tools/tcc.sh src)。tccdefs_.h はホストの tcc が作った
# ものを使う —— これを我々の OS の上で作るのは 16.3 の別件である。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

src=tmp/tcc/src
out=tmp/s17
root=$out/root

# Makefile の 34 行が -c する翻訳単位 (docs/stage017-cc.md 16.1)
UNITS="tccpp tccgen tccdbg tccelf tccasm tccrun riscv64-gen riscv64-link riscv64-asm libtcc tcc"

need() { [ -e "$1" ] || { echo "error: $1 が無い ($2)" >&2; exit 1; }; }

do_root() {
    need "$src" "sh tools/tcc.sh src"
    need tmp/tcc/build/tccdefs_.h "sh tools/tcc.sh host"
    for f in pp16cmd pp17 cc15pcmd ld16cmd sh2.bin cc19 kernel24.bin; do
        need "tmp/build/$f" "sh tools/build.sh stage017"
    done
    rm -rf "$root"
    mkdir -p "$root/bin" "$root/include/sys" "$root/lib" "$root/t"
    cp tmp/build/pp16cmd  "$root/bin/pp16"
    cp tmp/build/pp17     "$root/bin/pp17"
    cp tmp/build/cc15pcmd "$root/bin/cc15p"
    cp tmp/build/ld16cmd  "$root/bin/ld16"
    cp tmp/build/sh2.bin  "$root/bin/sh2"
    cp tmp/build/sh2.bin  "$root/sh2"
    cp tmp/build/cc19     "$root/cc19"
    cp stage017/libc19/include/*.h     "$root/include/"
    cp stage017/libc19/include/sys/*.h "$root/include/sys/"
    # **木のうち .c と .h だけを載せる。** configure や .texi は要らない
    for f in "$src"/*.c "$src"/*.h "$src"/*.def; do
        [ -f "$f" ] && cp "$f" "$root/t/"
    done
    cp tmp/tcc/build/tccdefs_.h "$root/t/"
    cp stage015/tcc/config-stone.h "$root/t/config.h"
    echo "root: $(find "$root" -type f | wc -l) ファイル / $(du -sb "$root" | cut -f1) バイト" >&2
}

# その 1 本の入力が前と同じなら飛ばす
stampkey() {
    sha256sum "$src/$1.c" "$root/t/config.h" "$root/t/tccdefs_.h" \
        tmp/build/cc19 tmp/build/pp17 tmp/build/cc15pcmd tmp/build/ld16cmd \
        tmp/build/kernel24.bin 2> /dev/null | sha256sum | cut -d' ' -f1
}

do_unit() {
    u=$1
    [ -d "$root" ] || do_root
    mkdir -p "$out/obj"
    key=$(stampkey "$u")
    if [ -z "${STONE_FORCE_TCC17:-}" ] && [ -s "$out/obj/$u.o" ] \
        && [ "$(cat "$out/step-$u.stamp" 2> /dev/null)" = "$key" ]; then
        echo "cached $u ($out/obj/$u.o)" >&2
        return 0
    fi
    # tcc.c と libtcc.c だけ -DONE_SOURCE=0 が要る (16.1 の 34 行のとおり)
    d=
    case $u in tcc|libtcc) d=" -D ONE_SOURCE=0" ;; esac
    printf 'cc19 -c t/%s.c -o t/%s.o -I t%s\necho "rc $?"\n' "$u" "$u" "$d" \
        > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/$u.log" 2>&1 || true
    # 走った後の像から .o を取り出す
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if grep -q '^rc 0$' "$out/$u.log" && [ -s "$out/back/t/$u.o" ]; then
        cp "$out/back/t/$u.o" "$out/obj/$u.o"
        echo "$key" > "$out/step-$u.stamp"
        echo "built $u ($(wc -c < "$out/obj/$u.o") バイト)" >&2
        return 0
    fi
    echo "FAIL $u ($out/$u.log):" >&2
    sed -n '1,12p' "$out/$u.log" >&2
    return 1
}

case ${1:-all} in
root) do_root ;;
unit) do_unit "${2:?usage: tcc17.sh unit <名前>}" ;;
clean) rm -rf "$out" ;;
all)
    do_root
    fail=0
    for u in $UNITS; do do_unit "$u" || fail=$((fail + 1)); done
    echo "---- $(ls "$out/obj" 2>/dev/null | wc -l) / $(echo $UNITS | wc -w) 本" >&2
    [ "$fail" -eq 0 ]
    ;;
*) echo "usage: tcc17.sh [root|unit <名前>|all|clean]" >&2; exit 2 ;;
esac
