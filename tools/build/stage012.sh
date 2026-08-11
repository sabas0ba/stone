# stage012 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage012 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 12 は Stage 8 の ld でリンカの新世代 (ld12) を作り，pp + cc で
# カーネルを作る。カーネルは 'K' 形式 (フラット + カーネル前置部) で，
# ユーザプログラムは 'E' 形式 (ELF 実行形式) でリンクする
# (docs/stage012-os.md 5.3)
build_stage012() {
    { cat stage012/ld12.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/ld12.o
    { cat tmp/build/ld12.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld12.bin
    echo "built tmp/build/ld12.bin" >&2
    sh tools/bundle.sh stage012/kernel.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/kernel.i
    sh tools/env.sh qemu tmp/build/cc.bin < tmp/build/kernel.i > tmp/build/kernel.o
    { printf 'K'; cat tmp/build/kernel.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld12.bin > tmp/build/kernel.bin
    echo "built tmp/build/kernel.bin" >&2
    # 第 12 世代の libc。純粋部 (src) と環境部 (posix) を持つ
    for f in src/string src/ctype src/stdlib src/morecore posix/sys posix/morecore posix/stdio; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage012/libc/include/*.h "stage012/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l12_$n.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/l12_$n.i" > "tmp/build/l12_$n.o"
        echo "built tmp/build/l12_$n.o" >&2
    done
}

do_stage012() {
    run_stage stage012 ld12.bin kernel.bin l12_src_string.o l12_src_ctype.o \
        l12_src_stdlib.o l12_src_morecore.o l12_posix_sys.o l12_posix_morecore.o \
        l12_posix_stdio.o \
        -- stage012/ld12.sc stage012/kernel.c stage012/libc/include/*.h \
           stage012/libc/src/*.c stage012/libc/posix/*.c tmp/build/stage008.stamp \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tmp/build/stage011.stamp tools/build/stage012.sh tools/bundle.sh
}
