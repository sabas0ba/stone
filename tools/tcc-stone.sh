#!/bin/sh
# tcc を**我々の鎖で**ビルドする (Stage 15 第 6 部)。
#
#   tcc-stone.sh t1     T1 を作る (pp16 -> cc15m -> ld15)。tmp/s16/tcc1.bin
#   tcc-stone.sh run    T1 を OS 上で走らせて in.c を翻訳し，ホストの
#                       riscv32-tcc の出力と突き合わせる
#
# tools/tcc.sh (ホストの gcc で作る開発道具) とは別物である。こちらは
# **ブートストラップ鎖の一部**であり，入力は我々の処理系の成果物だけ。
#
# 素材は docs/external/tcc に要る (無ければ tools/fetch.sh tcc)。
# patch を当てた木は tmp/tcc/src (tools/tcc.sh src が作る)。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

src=tmp/tcc/src
out=tmp/s16
inc=stage015/libc/include

need() {
    [ -e "$1" ] || { echo "error: $1 が無い ($2)" >&2; exit 1; }
}

prepare() {
    need docs/external/tcc "sh tools/fetch.sh tcc"
    [ -d "$src" ] || sh tools/tcc.sh src
    mkdir -p "$out"
    for f in cc15m.bin pp16.bin ld15.bin; do
        need "tmp/build/$f" "sh tools/build.sh stage015"
    done
}

# ONE_SOURCE の束ね。並べた最後が翻訳単位で，前のものは #include の対象。
# config.h は stone 用のものを載せる (上流の configure はホスト専用)
bundle() {
    sh tools/bundle.sh \
        $inc/stddef.h $inc/stdarg.h $inc/stdlib.h $inc/stdio.h \
        $inc/string.h $inc/errno.h $inc/math.h $inc/fcntl.h \
        $inc/setjmp.h $inc/time.h $inc/unistd.h $inc/ctype.h \
        $inc/limits.h $inc/assert.h $inc/inttypes.h \
        "sys/time.h=$inc/sys/time.h" \
        "config.h=stage015/tcc/config-stone.h" \
        "$src/elf.h" "$src/stab.h" "$src/stab.def" "$src/dwarf.h" \
        "$src/libtcc.h" "$src/tcctok.h" "$src/riscv64-tok.h" "$src/tcc.h" \
        tmp/tcc/build/tccdefs_.h \
        "$src/tccpp.c" "$src/tccgen.c" "$src/tccdbg.c" "$src/tccasm.c" \
        "$src/tccelf.c" "$src/tccrun.c" \
        "$src/riscv64-gen.c" "$src/riscv64-link.c" "$src/riscv64-asm.c" \
        "$src/libtcc.c" "$src/tcctools.c" \
        "$src/tcc.c"
}

do_t1() {
    prepare
    need tmp/tcc/build/tccdefs_.h "sh tools/tcc.sh host"
    bundle > "$out/tcc.bundle"
    sh tools/env.sh qemu tmp/build/pp16.bin < "$out/tcc.bundle" > "$out/tcc.i"
    echo "preprocessed: $(wc -l < "$out/tcc.i") 行" >&2
    sh tools/env.sh qemu tmp/build/cc15m.bin < "$out/tcc.i" > "$out/tcc.o"
    echo "compiled: $(wc -c < "$out/tcc.o") バイト" >&2
    # libc15 を同じ世代でコンパイルする
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh $inc/*.h "sys/time.h=$inc/sys/time.h" \
            "stage015/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/l_$n.i"
        sh tools/env.sh qemu tmp/build/cc15m.bin < "$out/l_$n.i" > "$out/l_$n.o"
    done
    { printf 'E'; cat "$out/tcc.o" \
        "$out/l_src_string.o" "$out/l_src_ctype.o" "$out/l_src_stdlib.o" \
        "$out/l_src_misc15.o" "$out/l_posix_sys.o" \
        "$out/l_posix_morecore.o" "$out/l_posix_stdio.o" \
        "$out/l_posix_assert.o" tmp/build/rt64.o tmp/build/rtfp.o; \
      printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld15.bin > "$out/tcc1.bin"
    echo "built $out/tcc1.bin ($(wc -c < "$out/tcc1.bin") バイト)" >&2
}

# sfs の像を組んで kernel15 の上で走らせる。$1 は boot に書く 1 行
run_os() {
    rm -rf "$out/root"
    mkdir -p "$out/root"
    shift_files=$1
    shift
    # shellcheck disable=SC2086
    for f in $shift_files; do cp "$f" "$out/root/$(basename "$f")"; done
    printf '%s\n' "$1" > "$out/root/boot"
    sh tools/sfs.sh pack "$out/root" "$out/fs.img" 4194304 128 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=134217728 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE="$out/ram" sh tools/env.sh qemu tmp/build/kernel15.bin \
        < /dev/null
}

# 像から sfs を取り出す (走らせた後にできたファイルを見る)
unpack_os() {
    dd if="$out/ram" of="$out/fs2.img" bs=64K iflag=skip_bytes \
        skip=67108864 count=64 2> /dev/null
    rm -rf "$out/root2"
    sh tools/sfs.sh unpack "$out/fs2.img" "$out/root2" > /dev/null
}

do_run() {
    [ -f "$out/tcc1.bin" ] || do_t1
    cat > "$out/in.c" <<'EOF'
int add(int a, int b) { return a + b; }
int main() { return add(40, 2); }
EOF
    tccdefs=$src/include/tccdefs.h
    [ -f "$tccdefs" ] || tccdefs=$src/tccdefs.h
    run_os "$out/tcc1.bin $out/in.c $tccdefs" 'tcc1.bin -I/ -c in.c -o out.o'
    unpack_os
    [ -f "$out/root2/out.o" ] || { echo "error: out.o が出ていない" >&2; exit 1; }
    # ホストの riscv32-tcc と突き合わせる。ファイル名がシンボルに入るので
    # 同じ名前・同じ位置で翻訳する
    ( cd "$out" && ../../tmp/tcc/build/riscv32-tcc -c in.c -o ref.o )
    if cmp -s "$out/root2/out.o" "$out/ref.o"; then
        echo "T1 の出力はホストの riscv32-tcc とバイト一致した" >&2
    else
        echo "error: 出力が食い違う" >&2
        exit 1
    fi
}

case "${1:-t1}" in
t1)  do_t1 ;;
run) do_run ;;
*)   echo "usage: tcc-stone.sh [t1|run]" >&2; exit 1 ;;
esac
