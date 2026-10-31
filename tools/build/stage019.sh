# stage019 のビルド手順と，入力・生成物の宣言。
#
# stone の中で Linux を組むための道具 (docs/stage019-linux.md 7 章)。
#
#   npp1      前処理器 (S1。8 章)
#   ncc       gcc の命令行を受けるドライバ (S2。9 章)
#   mk21      make の新しい世代 (S3。10 章)
#   kernel30  spawn の引数の上限を広げたカーネル (S2。9 章)
#
# npp1 と ncc は stage018 と同じく libc26 と cc15as で組み，前処理は stage016 の
# pp (tmp/build/pp16.bin) で行う。kernel30 は kernel29 と同じ手順 (cc15p と
# ld16。前置部は 'K') で組む

# stage019/<名前>.c を OS の実行形式 tmp/build/<名前> に組む
prog19_run() {
    sh tools/bundle.sh stage017/libc26/include/*.h \
        "sys/time.h=stage017/libc26/include/sys/time.h" \
        "sys/times.h=stage017/libc26/include/sys/times.h" \
        "sys/stat.h=stage017/libc26/include/sys/stat.h" \
        "sys/types.h=stage017/libc26/include/sys/types.h" \
        "stage019/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/$1.i"
    sh tools/env.sh qemu tmp/build/cc15as.bin < "tmp/build/$1.i" \
        > "tmp/build/$1.o"
    nonempty "tmp/build/$1.o" || return 1
    sh tools/ld18.sh -o "tmp/build/$1" -L tmp/build/l26_*.o tmp/build/rt64.o \
        tmp/build/rtfp.o -N "tmp/build/$1.o"
}

# カーネルを 1 つ作る (前置部は 'K')。stage017.sh の kern17 と同じ方法だが，
# この階層のことはこの階層で書く
kern19() {
    sh tools/bundle.sh "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'K'; cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

build_stage019() {
    for p in npp1 ncc mk21; do
        step "$p" "$p" \
            -- "stage019/$p.c" stage017/libc26/include/*.h \
               stage017/libc26/include/sys/*.h \
               tmp/build/pp16.bin tmp/build/cc15as.bin tmp/build/ld18.bin \
               tmp/build/l26_*.o tmp/build/rt64.o tmp/build/rtfp.o \
               tools/ld18.sh \
            -- prog19_run "$p" || return 1
    done
    step kernel30 kernel30.bin \
        -- stage019/kernel30.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kern19 kernel30 stage019/kernel30.c || return 1
}

do_stage019() {
    run_stage stage019 npp1 ncc mk21 kernel30.bin \
        -- stage019/npp1.c stage019/ncc.c stage019/mk21.c stage019/kernel30.c \
           stage017/libc26/include/*.h stage017/libc26/include/sys/*.h \
           tmp/build/stage018.stamp tools/build/stage019.sh tools/bundle.sh \
           tools/ld18.sh
}
