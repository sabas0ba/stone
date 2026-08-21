# stage017 のビルド手順と，入力・生成物の宣言。
#
# 第 1 部は**コンパイラをコマンドとして持つ**ことである
# (docs/stage017-cc.md)。作るものは 2 種類ある。
#
#   1. pp16cmd / cc15pcmd / ld16cmd
#      既にある .o を 'E' で組み直しただけのもの。ソースは無い。
#      tmp/build の pp16.bin / cc15p.bin / ld16.bin は**平らな像**で
#      あり，QEMU に -bios で直に置いて走らせる形をしている。OS の
#      上の実行形式にするには ELF (前置部 'E') でリンクし直す必要が
#      ある。Stage 13 の pp13cmd / cc13cmd / ld13cmd と同じ手である
#      (docs/stage013-tools.md 7 章)。
#
#   2. cc17
#      駆動役。上の 3 つを順に呼ぶ。libc の第 18 世代とリンクする。

# 既にある .o を OS の実行形式へ組み直す。cmdlink <名前> <元の .o の名前>
cmdlink() {
    step "$1" "$1" \
        -- "tmp/build/${2}.o" tmp/build/ld16.bin \
        -- cmdlink_run "$1" "$2"
}

cmdlink_run() {
    { printf 'E'; cat "tmp/build/${2}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
}

build_stage017() {
    cmdlink pp16cmd pp16
    cmdlink cc15pcmd cc15p
    cmdlink ld16cmd ld16

    step cc17 cc17 \
        -- stage017/cc17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- cc17_run
}

cc17_run() {
    sh tools/bundle.sh stage016/libc18/include/*.h \
        "sys/stat.h=stage016/libc18/include/sys/stat.h" \
        stage017/cc17.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/cc17.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/cc17.i \
        > tmp/build/cc17.o
    # **/lib へ置く 1 揃いと同じ並びである。** ここと tests/stage017 の
    # /lib が食い違うと，駆動役が組んだものだけ挙動が変わる
    { printf 'E'; cat tmp/build/cc17.o \
        tmp/build/l18_src_string.o tmp/build/l18_src_stdlib.o \
        tmp/build/l18_src_misc15.o tmp/build/l18_posix_sys.o \
        tmp/build/l18_posix_morecore.o tmp/build/l18_posix_stdio.o \
        tmp/build/l18_posix_assert.o tmp/build/l18_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/cc17
    echo "built tmp/build/cc17" >&2
}

do_stage017() {
    run_stage stage017 pp16cmd cc15pcmd ld16cmd cc17 \
        -- stage017/cc17.c stage016/libc18/include/*.h \
           stage016/libc18/include/sys/*.h \
           tmp/build/stage016.stamp tools/build/stage017.sh tools/bundle.sh
}
