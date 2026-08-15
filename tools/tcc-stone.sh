#!/bin/sh
# tcc を**我々の鎖で**ビルドする (Stage 15 第 6 部)。
#
#   tcc-stone.sh t1     T1 を作る (pp16 -> cc15o -> ld15)。tmp/s16/tcc1.bin
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
    for f in cc15o.bin pp16.bin ld16.bin; do
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
    sh tools/env.sh qemu tmp/build/cc15o.bin < "$out/tcc.i" > "$out/tcc.o"
    echo "compiled: $(wc -c < "$out/tcc.o") バイト" >&2
    # libc15 を同じ世代でコンパイルする
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh $inc/*.h "sys/time.h=$inc/sys/time.h" \
            "stage015/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp16.bin > "$out/l_$n.i"
        sh tools/env.sh qemu tmp/build/cc15o.bin < "$out/l_$n.i" > "$out/l_$n.o"
    done
    { printf 'E'; cat "$out/tcc.o" \
        "$out/l_src_string.o" "$out/l_src_ctype.o" "$out/l_src_stdlib.o" \
        "$out/l_src_misc15.o" "$out/l_posix_sys.o" \
        "$out/l_posix_morecore.o" "$out/l_posix_stdio.o" \
        "$out/l_posix_assert.o" tmp/build/rt64.o tmp/build/rtfp.o; \
      printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "$out/tcc1.bin"
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

# T2 の作業場 (tcc のソース一式 + T1 + ヘッダ) を組む。
# 平らな名前空間なので stdarg.h / stddef.h は **tcc 自身のもの**を置く
# (我々の stdarg.h は cc 専用の隠しローカル __va_ptr を使うため)
t2tree() {
    prepare
    [ -f "$out/tcc1.bin" ] || do_t1
    rm -rf "$out/t2fs"
    mkdir -p "$out/t2fs/sys"
    for f in tcc.c libtcc.c tccpp.c tccgen.c tccdbg.c tccasm.c tccelf.c \
             tccrun.c tcctools.c riscv64-gen.c riscv64-link.c riscv64-asm.c \
             tcc.h libtcc.h elf.h stab.h stab.def dwarf.h tcctok.h \
             riscv64-tok.h; do
        cp "$src/$f" "$out/t2fs/"
    done
    cp "$src"/include/*.h "$out/t2fs/"
    for h in $inc/*.h; do
        b=$(basename "$h")
        case $b in stdarg.h|stddef.h) continue ;; esac
        cp "$h" "$out/t2fs/"
    done
    cp "$inc/sys/time.h" "$out/t2fs/sys/time.h"
    cp stage015/tcc/config-stone.h "$out/t2fs/config.h"
    cp tmp/tcc/build/tccdefs_.h "$out/t2fs/"   # tccpp.c が文字列として取り込む
    cp "$out/tcc1.bin" "$out/t2fs/tcc1"
    # 実行環境の材料。T1 自身に翻訳・アセンブルさせる
    cp stage015/tccrt/start.S "$out/t2fs/"
    cp "$src"/lib/libtcc1.c "$src"/lib/riscv32.c "$out/t2fs/"
    for f in src/string src/ctype src/stdlib src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert; do
        cp "stage015/libc/$f.c" "$out/t2fs/"
    done
    cp tmp/build/sh13 "$out/t2fs/sh"
}

# 作業場でシェルにスクリプトを食わせる (boot 行は 8 語までなので，
# 手順が 1 行に収まらないものはこちらを使う)。sh は 1 行 9 語まで。
t2sh() {
    printf 'sh\n' > "$out/t2fs/boot"
    sh tools/sfs.sh pack "$out/t2fs" "$out/t2fs.img" 8388608 256 > /dev/null
    rm -f "$out/t2ram"
    dd if=/dev/null of="$out/t2ram" bs=1 seek=134217728 2> /dev/null
    dd if="$out/t2fs.img" of="$out/t2ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    { cat "$1"; printf '\004'; } \
        | STONE_QEMU_RAMFILE="$out/t2ram" sh tools/env.sh qemu \
            tmp/build/kernel16.bin
    dd if="$out/t2ram" of="$out/t2fs2.img" bs=64K iflag=skip_bytes \
        skip=67108864 count=256 2> /dev/null
    rm -rf "$out/t2out"
    sh tools/sfs.sh unpack "$out/t2fs2.img" "$out/t2out" > /dev/null
}

# T1 に **tcc の実行形式まるごと**を作らせる (= T2)。
# 手順は tools/tcc.sh os の tccH と同じでなければならない (直に比べる
# ため)。シェルの 1 行は 9 語までなので -r で畳んでから繋ぐ。
t2script() {
    cat <<'EOF'
tcc1 -c start.S -o start.o
tcc1 -nostdinc -I/ -c riscv32.c -o riscv32.o
tcc1 -nostdinc -I/ -c libtcc1.c -o libtcc1.o
tcc1 -nostdinc -I/ -c string.c -o string.o
tcc1 -nostdinc -I/ -c ctype.c -o ctype.o
tcc1 -nostdinc -I/ -c stdlib.c -o stdlib.o
tcc1 -nostdinc -I/ -c misc15.c -o misc15.o
tcc1 -nostdinc -I/ -c sys.c -o sys.o
tcc1 -nostdinc -I/ -c morecore.c -o morecore.o
tcc1 -nostdinc -I/ -c stdio.c -o stdio.o
tcc1 -nostdinc -I/ -c assert.c -o assert.o
tcc1 -nostdinc -I/ -c tcc.c -o tcc.o
tcc1 -r -o libc1.o string.o ctype.o stdlib.o misc15.o
tcc1 -r -o libc2.o sys.o morecore.o stdio.o assert.o
tcc1 -r -o rt.o start.o riscv32.o libtcc1.o
tcc1 -r -o all.o rt.o tcc.o libc1.o libc2.o
tcc1 -nostdlib -static -Wl,-Ttext=0x86000000 -o tcc2 all.o
exit
EOF
}

# 作業場で 1 つコマンドを走らせ，結果のファイル木を $out/t2out へ出す。
# 引数はカーネルの boot 行に書ける 8 語まで
t2exec() {
    printf '%s\n' "$1" > "$out/t2fs/boot"
    sh tools/sfs.sh pack "$out/t2fs" "$out/t2fs.img" 8388608 128 > /dev/null
    rm -f "$out/t2ram"
    dd if=/dev/null of="$out/t2ram" bs=1 seek=134217728 2> /dev/null
    dd if="$out/t2fs.img" of="$out/t2ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE="$out/t2ram" sh tools/env.sh qemu tmp/build/kernel16.bin \
        < /dev/null
    rc=$?
    dd if="$out/t2ram" of="$out/t2fs2.img" bs=64K iflag=skip_bytes \
        skip=67108864 count=128 2> /dev/null
    rm -rf "$out/t2out"
    sh tools/sfs.sh unpack "$out/t2fs2.img" "$out/t2out" > /dev/null
    return $rc
}

# T1 に tcc 自身を翻訳させ，ホストの riscv32-tcc の出力と突き合わせる。
# **まだ一致しない** (docs/stage015-tcc.md 12.12)
do_t2() {
    t2tree
    t2exec 'tcc1 -I/ -c tcc.c -o tcc2.o' 2>&1 | grep -v 'warning\|In file included' || true
    [ -f "$out/t2out/tcc2.o" ] || { echo "error: tcc2.o が出ていない" >&2; exit 1; }
    ( cd "$out/t2fs" && ../../../tmp/tcc/build/riscv32-tcc -nostdinc -I. \
        -c tcc.c -o ../t2ref.o ) 2> /dev/null
    echo "T1 の出力: $(wc -c < "$out/t2out/tcc2.o") バイト" >&2
    echo "ホスト   : $(wc -c < "$out/t2ref.o") バイト" >&2
    cmp -s "$out/t2out/tcc2.o" "$out/t2ref.o" \
        && echo "一致した" >&2 || echo "まだ食い違う (12.12)" >&2
}

# tccH (ホストの交差 tcc が作った，我々の OS 用の tcc) を OS の上で
# 走らせる。**これは鎖の検査ではない。** 実行環境 (start.S・libc15・
# kernel16 の ELF 読み・libtcc1 相当) が揃っているかだけを見る対照で
# ある。ここが通れば，T2 が動かないときの原因は我々の cc に絞れる。
do_th() {
    need tmp/build/kernel16.bin "sh tools/build.sh stage015"
    [ -f tmp/tcc/build/tccH ] || sh tools/tcc.sh os
    mkdir -p "$out"
    rm -rf "$out/thfs"
    mkdir -p "$out/thfs"
    cp tmp/tcc/build/tccH "$out/thfs/tccH"
    cat > "$out/thfs/in.c" <<'EOF'
int add(int a, int b) { return a + b; }
int main() { return add(40, 2); }
EOF
    tccdefs=$src/include/tccdefs.h
    [ -f "$tccdefs" ] || tccdefs=$src/tccdefs.h
    cp "$tccdefs" "$out/thfs/tccdefs.h"
    printf 'tccH -I/ -c in.c -o out.o\n' > "$out/thfs/boot"
    sh tools/sfs.sh pack "$out/thfs" "$out/thfs.img" 8388608 128 > /dev/null
    rm -f "$out/thram"
    dd if=/dev/null of="$out/thram" bs=1 seek=134217728 2> /dev/null
    dd if="$out/thfs.img" of="$out/thram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE="$out/thram" sh tools/env.sh qemu \
        tmp/build/kernel16.bin < /dev/null
    dd if="$out/thram" of="$out/thfs2.img" bs=64K iflag=skip_bytes \
        skip=67108864 count=128 2> /dev/null
    rm -rf "$out/thout"
    sh tools/sfs.sh unpack "$out/thfs2.img" "$out/thout" > /dev/null
    [ -f "$out/thout/out.o" ] || { echo "error: out.o が出ていない" >&2; exit 1; }
    ( cd "$out/thfs" && ../../../tmp/tcc/build/riscv32-tcc -c in.c -o ../thref.o )
    if cmp -s "$out/thout/out.o" "$out/thref.o"; then
        echo "tccH は OS の上でホストの riscv32-tcc と同じ .o を出した" >&2
    else
        echo "error: tccH の出力がホストと食い違う" >&2
        exit 1
    fi
}

# T2 を作る。T1 に実行環境 (start.S・libtcc1 相当・libc15) と tcc 本体を
# 翻訳させ，繋いで実行形式にする。手順は tccH と同一である。
do_t2b() {
    t2tree
    t2script > "$out/t2.sh"
    t2sh "$out/t2.sh" 2>&1 | grep -v 'warning\|In file included' || true
    [ -f "$out/t2out/tcc2" ] || { echo "error: tcc2 ができていない" >&2; exit 1; }
    cp "$out/t2out/tcc2" "$out/tcc2.bin"
    echo "built $out/tcc2.bin ($(wc -c < "$out/tcc2.bin") バイト)" >&2
    if [ -f tmp/tcc/build/tccH ]; then
        cmp -s "$out/tcc2.bin" tmp/tcc/build/tccH \
            && echo "T2 は tccH とバイト一致した" >&2 \
            || echo "T2 は tccH と食い違う (振舞いが同じなら T2 == T3 は成る)" >&2
    fi
}

# T3 を作る。**T2 に同じ手順で tcc を作らせる。** T2 == T3 が第 6 部の
# 完了条件である (tcc が tcc を作り，その出力が動かない = 固定点)。
#
# 手順は t2script と 1 語だけ違う (翻訳器が tcc1 か tcc2 か，出力の名前が
# tcc2 か tcc3 か)。入力のファイル名は同じでなければならない —— tcc は
# 記号表にファイル名を入れるためである。出力の名前は像に入らない。
do_t3() {
    [ -f "$out/tcc2.bin" ] || do_t2b
    t2tree                              # 作業場を作り直す (中身は T2 と同一)
    cp "$out/tcc2.bin" "$out/t2fs/tcc2"
    t2script | sed 's/^tcc1 /tcc2 /; s/-o tcc2 /-o tcc3 /' > "$out/t3.sh"
    t2sh "$out/t3.sh" 2>&1 | grep -v 'warning\|In file included' || true
    [ -f "$out/t2out/tcc3" ] || { echo "error: tcc3 ができていない" >&2; exit 1; }
    cp "$out/t2out/tcc3" "$out/tcc3.bin"
    echo "built $out/tcc3.bin ($(wc -c < "$out/tcc3.bin") バイト)" >&2
    if cmp -s "$out/tcc2.bin" "$out/tcc3.bin"; then
        echo "T2 == T3 —— tcc の自己ホストが成った" >&2
    else
        echo "T2 != T3 (T2=$(wc -c < "$out/tcc2.bin") T3=$(wc -c < "$out/tcc3.bin"))" >&2
        exit 1
    fi
}

case "${1:-t1}" in
t1)  do_t1 ;;
run) do_run ;;
t2)  do_t2 ;;
t2b) do_t2b ;;
t3)  do_t3 ;;
th)  do_th ;;
*)   echo "usage: tcc-stone.sh [t1|run|t2|t2b|t3|th]" >&2; exit 1 ;;
esac
