# stage016 のビルド手順と，入力・生成物の宣言。
#
# 第 1 部はファイル系 (sfs2)。カーネルの新世代 kernel17 が sfs2 を読み，
# 経路をディレクトリの木として解決する (docs/stage016-os.md 6 章)。
# 第 2 部はディレクトリの操作。kernel18 が作業ディレクトリと
# getdents64 / mkdirat / chdir / getcwd を持ち，libc16 がそれを包む
# (7 章)。どちらも最前線の cc15p / pp16 / ld16 で作る。
#
# 世代ごとに step のスタンプを持つので，途中で殺されても失うのは
# 高々 1 つである (docs/dev-notes.md 1.5)。

# カーネルを 1 つ作る (前置部は 'K')。kern <名前> <ソース>
kern() {
    step "$1" "${1}.bin" \
        -- "$2" tmp/build/cc15p.bin tmp/build/pp16.bin tmp/build/ld16.bin \
        -- kern_run "$1" "$2"
}

kern_run() {
    sh tools/bundle.sh "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'K'; cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

build_stage016() {
    kern kernel17 stage016/kernel17.c        # 第 1 部
    kern kernel18 stage016/kernel18.c        # 第 2 部
    kern kernel19 stage016/kernel19.c        # 第 3 部 (記憶域の拡張)

    # libc の第 16 世代。libc15 との差は dirent / mkdir / chdir / getcwd と，
    # open が先頭の '/' を剥がすのをやめたこと (docs/stage016-os.md 7.4)。
    # 翻訳は libc15 と同じ cc15k で行う (器の都合。stage015c.sh と揃える)
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir; do
        n=$(echo "$f" | tr / _)
        step "l16_$n" "l16_$n.o" \
            -- "stage016/libc/$f.c" tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc16_run "$f" "$n"
    done
}

libc16_run() {
    sh tools/bundle.sh stage016/libc/include/*.h \
        "sys/time.h=stage016/libc/include/sys/time.h" \
        "sys/stat.h=stage016/libc/include/sys/stat.h" \
        "stage016/libc/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l16_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l16_$2.i" \
        > "tmp/build/l16_$2.o"
    echo "built tmp/build/l16_$2.o" >&2
}

do_stage016() {
    run_stage stage016 kernel17.bin kernel18.bin kernel19.bin \
        l16_src_string.o l16_src_ctype.o l16_src_stdlib.o \
        l16_src_morecore.o l16_src_misc15.o \
        l16_posix_sys.o l16_posix_morecore.o l16_posix_stdio.o \
        l16_posix_assert.o l16_posix_dir.o \
        -- stage016/kernel17.c stage016/kernel18.c stage016/kernel19.c \
           stage016/libc/include/*.h stage016/libc/include/sys/*.h \
           stage016/libc/src/*.c stage016/libc/posix/*.c \
           tmp/build/stage015c.stamp tools/build/stage016.sh
}
