# stage015c のビルド手順と，入力・生成物の宣言。
#
# OS の側の成果物 (実行時支援・カーネル・libc15)。原文が C なので
# 手を入れる回数がいちばん多い。ここを触っても cc の世代 (stage015a /
# stage015b) は作り直されない。

build_stage015c() {
    # 実行時支援 (64 bit の除算の実体)。cc15e 自身でコンパイルする。
    # ブロックコメントを含むので pp を通す (cc は // しか解さない)
    sh tools/bundle.sh stage015/rt64.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/rt64.i
    sh tools/env.sh qemu tmp/build/cc15e.bin < tmp/build/rt64.i > tmp/build/rt64.o
    echo "built tmp/build/rt64.o" >&2

    # カーネル第 15 世代 (lseek。第 4 部)。libc と同じく最前線で作る
    sh tools/bundle.sh stage015/kernel15.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/kernel15.i
    sh tools/env.sh qemu tmp/build/cc15k.bin < tmp/build/kernel15.i > tmp/build/kernel15.o
    { printf 'K'; cat tmp/build/kernel15.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld14.bin > tmp/build/kernel15.bin
    echo "built tmp/build/kernel15.bin" >&2
    # カーネル第 16 世代 (第 6 部)。tcc の出す ELF は PT_LOAD を 2〜3 本
    # 持つが kernel15 は先頭しか載せない。全部載せるようにしたもの
    # (docs/stage015-tcc.md 12.10)。最前線の cc15p / ld16 で作る
    sh tools/bundle.sh stage015/kernel16.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/kernel16.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/kernel16.i > tmp/build/kernel16.o
    { printf 'K'; cat tmp/build/kernel16.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/kernel16.bin
    echo "built tmp/build/kernel16.bin" >&2
    # 第 15 世代の libc (第 4 部の実測ぶん)。最前線の cc15k でコンパイル
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage015/libc/include/*.h \
            "sys/time.h=stage015/libc/include/sys/time.h" "stage015/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l15_$n.i"
        sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l15_$n.i" > "tmp/build/l15_$n.o"
        echo "built tmp/build/l15_$n.o" >&2
    done
    # 浮動小数点の実行時支援 (第 3 部)。浮動小数点の型を使わずに書いてある
    # ので，第 2 部までの cc でそのまま通る (docs/stage015-tcc.md 10.3)
    sh tools/bundle.sh stage015/rtfp.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/rtfp.i
    sh tools/env.sh qemu tmp/build/cc15e.bin < tmp/build/rtfp.i > tmp/build/rtfp.o
    echo "built tmp/build/rtfp.o" >&2
}

do_stage015c() {
    run_stage stage015c rt64.o rtfp.o kernel15.bin kernel16.bin \
        l15_src_string.o l15_src_ctype.o l15_src_stdlib.o l15_src_morecore.o \
        l15_src_misc15.o l15_posix_sys.o l15_posix_morecore.o \
        l15_posix_stdio.o l15_posix_assert.o \
        -- stage015/kernel15.c stage015/kernel16.c stage015/libc/include/*.h \
           stage015/libc/include/sys/*.h \
           stage015/libc/src/*.c stage015/libc/posix/*.c \
           stage015/rt64.c stage015/rtfp.c \
           tmp/build/stage015b.stamp tools/build/stage015c.sh tools/bundle.sh
}
