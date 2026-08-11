# stage013 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage013 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 13 はリンカの新世代 (ld13 = ld12 + sys_ecall) を Stage 8 の ld で，
# カーネル (kernel13 = spawn とつなぎ替え) とシェル (sh13) を pp + cc +
# ld13 で作る (docs/stage013-tools.md 3 章)。libc は第 13 世代
# (stage013/libc。spawn の包みを足した) をオブジェクトのまま置く
build_stage013() {
    { cat stage013/ld13.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/ld13.o
    { cat tmp/build/ld13.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld13.bin
    echo "built tmp/build/ld13.bin" >&2
    sh tools/bundle.sh stage013/kernel.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/kernel13.i
    sh tools/env.sh qemu tmp/build/cc.bin < tmp/build/kernel13.i > tmp/build/kernel13.o
    { printf 'K'; cat tmp/build/kernel13.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld13.bin > tmp/build/kernel13.bin
    echo "built tmp/build/kernel13.bin" >&2
    # 第 13 世代の libc
    for f in src/string src/ctype src/stdlib src/morecore posix/sys posix/morecore posix/stdio; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage013/libc/include/*.h "stage013/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l13_$n.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/l13_$n.i" > "tmp/build/l13_$n.o"
        echo "built tmp/build/l13_$n.o" >&2
    done
    # OS 上の道具 (ELF 実行形式)。どれも libc の第 13 世代を並べる
    for t in sh ed bundle ldin eot mk; do
        sh tools/bundle.sh stage013/libc/include/*.h "stage013/$t.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/${t}13.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/${t}13.i" > "tmp/build/${t}13.o"
        { printf 'E'; cat "tmp/build/${t}13.o" tmp/build/l13_src_string.o \
            tmp/build/l13_src_stdlib.o tmp/build/l13_posix_sys.o \
            tmp/build/l13_posix_morecore.o tmp/build/l13_posix_stdio.o; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld13.bin > "tmp/build/${t}13"
        echo "built tmp/build/${t}13" >&2
    done
    # 処理系そのものを OS 上のコマンドへ移す (docs/stage013-tools.md 7 章)。
    # ソースは無く，既にある .o を 'E' でリンクし直すだけである。
    # 'E' 前置部の getc / putc が read(0) / write(1) の 1 バイト版なので，
    # 「標準入力を読み標準出力へ書くフィルタ」という姿がそのまま通じる
    for pair in pp:pp cc:cc10l ld:ld13; do
        t=${pair%%:*}
        o=${pair##*:}
        { printf 'E'; cat "tmp/build/$o.o"; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld13.bin > "tmp/build/${t}13cmd"
        echo "built tmp/build/${t}13cmd" >&2
    done
}

do_stage013() {
    run_stage stage013 ld13.bin kernel13.bin l13_src_string.o l13_src_ctype.o \
        l13_src_stdlib.o l13_src_morecore.o l13_posix_sys.o l13_posix_morecore.o \
        l13_posix_stdio.o sh13 ed13 bundle13 ldin13 eot13 mk13 \
        pp13cmd cc13cmd ld13cmd \
        -- stage013/ld13.sc stage013/kernel.c stage013/sh.c stage013/ed.c \
           stage013/bundle.c stage013/ldin.c stage013/eot.c stage013/mk.c \
           stage013/libc/include/*.h stage013/libc/src/*.c \
           stage013/libc/posix/*.c tmp/build/stage008.stamp \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tools/build/stage013.sh tools/bundle.sh
}
