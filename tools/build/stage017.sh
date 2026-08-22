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

    # 第 2 部。cc17 は記録対象なので書き換えず世代を刻む
    # (roadmap.md 4 章)。ar17 は新しい道具なので第 1 世代
    step cc18 cc18 \
        -- stage017/cc18.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run cc18 stage017/cc18.c

    step ar17 ar17 \
        -- stage017/ar17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run ar17 stage017/ar17.c

    # make の第 17 世代 (第 3 部の 1)
    step mk17 mk17 \
        -- stage017/mk17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run mk17 stage017/mk17.c

    # カーネルの第 23 世代。kernel22 との差は引数の数と長さだけ
    # (docs/stage017-cc.md 8 章)。前置部は 'K' である
    step kernel23 kernel23.bin \
        -- stage017/kernel23.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kernel23_run
}

kernel23_run() {
    sh tools/bundle.sh stage017/kernel23.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/kernel23.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/kernel23.i \
        > tmp/build/kernel23.o
    { printf 'K'; cat tmp/build/kernel23.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/kernel23.bin
    echo "built tmp/build/kernel23.bin" >&2
}

# OS の上で動く実行形式を 1 つ作る。osprog_run <名前> <ソース>
#
# **並べる .o は sh2 / cc17 と同じ 1 揃いである。** ここと
# tests/stage017 の /lib が食い違うと，駆動役が組んだものだけ
# 挙動が変わる (docs/stage017-cc.md 3.2)
osprog_run() {
    sh tools/bundle.sh stage016/libc18/include/*.h \
        "sys/stat.h=stage016/libc18/include/sys/stat.h" \
        "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'E'; cat "tmp/build/${1}.o" \
        tmp/build/l18_src_string.o tmp/build/l18_src_stdlib.o \
        tmp/build/l18_src_misc15.o tmp/build/l18_posix_sys.o \
        tmp/build/l18_posix_morecore.o tmp/build/l18_posix_stdio.o \
        tmp/build/l18_posix_assert.o tmp/build/l18_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
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
    run_stage stage017 pp16cmd cc15pcmd ld16cmd cc17 cc18 ar17 mk17 \
        kernel23.bin \
        -- stage017/cc17.c stage017/cc18.c stage017/ar17.c \
           stage017/mk17.c stage017/kernel23.c \
           stage016/libc18/include/*.h \
           stage016/libc18/include/sys/*.h \
           tmp/build/stage016.stamp tools/build/stage017.sh tools/bundle.sh
}
