# stage018 のビルド手順と，入力・生成物の宣言。
#
# psABI の処理系 (docs/stage018-ncc.md)。アセンブラ nas1・リンカ nld1・
# C コンパイラ ncc1 と，Kbuild が呼ぶ周辺の道具 (書庫の nar1，nm・objcopy・
# objdump の nobj1) である。どれも C で書いてあり，libc26 と最前線の cc15as で
# 組む。前処理は stage016 の pp (tmp/build/pp16.bin) で行う。stage009 の
# pp.bin は出力の器が 192 KiB で，ncc1 の前処理の出力 (約 200 KB) が入らない。
# 器の大きさの他は同じ出力を出す (nld1.c で確かめた)

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
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/$1.i"
    sh tools/env.sh qemu tmp/build/cc15as.bin < "tmp/build/$1.i" \
        > "tmp/build/$1.o"
    nonempty "tmp/build/$1.o" || return 1
    sh tools/ld18.sh -o "tmp/build/$1" -L tmp/build/l26_*.o tmp/build/rt64.o \
        tmp/build/rtfp.o -N "tmp/build/$1.o"
}

build_stage018() {
    for p in nas1 nld1 ncc1 nar1 nobj1; do
        step "$p" "$p" \
            -- "stage018/$p.c" stage017/libc26/include/*.h \
               stage017/libc26/include/sys/*.h \
               tmp/build/pp16.bin tmp/build/cc15as.bin tmp/build/ld18.bin \
               tmp/build/l26_*.o tmp/build/rt64.o tmp/build/rtfp.o \
               tools/ld18.sh \
            -- prog18_run "$p" || return 1
    done
}

do_stage018() {
    run_stage stage018 nas1 nld1 ncc1 nar1 nobj1 \
        -- stage018/nas1.c stage018/nld1.c stage018/ncc1.c stage018/nar1.c stage018/nobj1.c \
           stage017/libc26/include/*.h stage017/libc26/include/sys/*.h \
           tmp/build/stage017.stamp tools/build/stage018.sh tools/bundle.sh \
           tools/ld18.sh
}
