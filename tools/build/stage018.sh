# stage018 のビルド手順と，入力・生成物の宣言。
#
# psABI の処理系 (docs/stage018-ncc.md)。今あるのはアセンブラ nas1 (M1) と
# リンカ nld1 (M2) である。どちらも C で書いてあり，libc26 と最前線の
# cc15as で組む。前処理は stage017 の pp (tmp/build/pp.bin) で行う。

# stage018/<名前>.c を OS の実行形式 tmp/build/<名前> に組む。
# libc26 と実行時ルーチンを先に並べる (前置部の syscall スタブを jal で
# 呼ぶため。stage015/ld18.md)
prog18_run() {
    sh tools/bundle.sh stage017/libc26/include/*.h \
        "sys/time.h=stage017/libc26/include/sys/time.h" \
        "sys/times.h=stage017/libc26/include/sys/times.h" \
        "sys/stat.h=stage017/libc26/include/sys/stat.h" \
        "sys/types.h=stage017/libc26/include/sys/types.h" \
        "stage018/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/$1.i"
    sh tools/env.sh qemu tmp/build/cc15as.bin < "tmp/build/$1.i" \
        > "tmp/build/$1.o"
    nonempty "tmp/build/$1.o" || return 1
    sh tools/ld18.sh -o "tmp/build/$1" -L tmp/build/l26_*.o tmp/build/rt64.o \
        tmp/build/rtfp.o -N "tmp/build/$1.o"
}

build_stage018() {
    for p in nas1 nld1; do
        step "$p" "$p" \
            -- "stage018/$p.c" stage017/libc26/include/*.h \
               stage017/libc26/include/sys/*.h \
               tmp/build/pp.bin tmp/build/cc15as.bin tmp/build/ld18.bin \
               tmp/build/l26_*.o tmp/build/rt64.o tmp/build/rtfp.o \
               tools/ld18.sh \
            -- prog18_run "$p" || return 1
    done
}

do_stage018() {
    run_stage stage018 nas1 nld1 \
        -- stage018/nas1.c stage018/nld1.c \
           stage017/libc26/include/*.h stage017/libc26/include/sys/*.h \
           tmp/build/stage017.stamp tools/build/stage018.sh tools/bundle.sh \
           tools/ld18.sh
}
