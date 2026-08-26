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
    # tcc の作業場で使う器。cc15p は静的な初期化子の文字列を壊すので
    # (docs/stage017-cc.md 24〜28 章)，tcc を正しく組むには cc15q が要る。
    # 鎖のプログラムは今までどおり cc15pcmd で組む
    cmdlink cc15qcmd cc15q
    # 第 18 世代。文字列リテラルの sizeof を直したもの。tcc が書庫を
    # 読むのにこれが要る (docs/stage017-cc.md 31 章)
    cmdlink cc15rcmd cc15r
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
    # libc の第 19 世代 (第 4 部の 1)。libc18 との差は stat だけ。
    # 翻訳は libc18 と同じ cc15k で行う (器の都合。stage016.sh と揃える)
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir; do
        n=$(echo "$f" | tr / _)
        step "l19_$n" "l19_$n.o" \
            -- "stage017/libc19/$f.c" \
               stage017/libc19/include/*.h \
               stage017/libc19/include/sys/time.h \
               stage017/libc19/include/sys/stat.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc19_run "$f" "$n"
    done

    # make の第 18 世代 (第 4 部の 2)。**libc19 と繋ぐ** (stat が要る)
    step mk18 mk18 \
        -- stage017/mk18.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk18 stage017/mk18.c

    # make の第 19 世代 (第 3 部の 2)。関数を持つ
    step mk19 mk19 \
        -- stage017/mk19.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk19 stage017/mk19.c

    # libc の第 20 世代 (第 3 部の 3 の 3)。libc19 との差は
    # 助言的ロック (fcntl) と getpid と EINTR だけ。tcc の lib/tcov.c が
    # 要る (docs/stage017-cc.md 27 章)
    #
    # **ヘッダも入力に数える。** libc*_run は include/*.h を束ねてから
    # 翻訳するので，ヘッダを直したら .o が変わりうる。ここに書かないと
    # 外側の stage の印だけが変わって build_stage017 が走り，中の step は
    # 「前と同じ」と言って**古い .o を持ち回ったまま新しい印が書かれる**。
    # 数えるのは束ねているものと同じ並びにすること
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir; do
        n=$(echo "$f" | tr / _)
        step "l20_$n" "l20_$n.o" \
            -- "stage017/libc20/$f.c" \
               stage017/libc20/include/*.h \
               stage017/libc20/include/sys/time.h \
               stage017/libc20/include/sys/stat.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc20_run "$f" "$n"
    done

    # 前処理器の第 17 世代 (第 3 部の 3 の 2)。-I を探す道として持つ。
    # **libc を繋がない** —— sys_* は 'E' 前置部のものを直に呼ぶ
    # (docs/stage017-cc.md 17 章)
    step pp17 pp17 \
        -- stage017/pp17.sc tmp/build/cc15p.bin tmp/build/ld16.bin \
        -- pp17_run

    # cc の第 19 世代 (第 3 部の 3 の 2)。-I を束ねず pp17 へ渡す
    step cc19 cc19 \
        -- stage017/cc19.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run cc19 stage017/cc19.c

    # make の第 20 世代 (第 3 部の 3 の 1)。tcc の Makefile を読む
    step mk20 mk20 \
        -- stage017/mk20.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk20 stage017/mk20.c

    # 時刻を読む検査用のプログラム (第 4 部の 1)。**libc19 と繋ぐ**
    step stamp stamp \
        -- tests/stage017/user/stamp.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run stamp tests/stage017/user/stamp.c

    step kernel23 kernel23.bin \
        -- stage017/kernel23.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kernel23_run

    # カーネルの第 24 世代。kernel23 との差は sfs3 と時刻だけ
    # (docs/stage017-cc.md 11 章)
    step kernel24 kernel24.bin \
        -- stage017/kernel24.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kern17 kernel24 stage017/kernel24.c
}

# カーネルを 1 つ作る (前置部は 'K')。stage016.sh の kern と同じ手だが，
# **この階層のことはこの階層で書く** (読む順に依らせない)
kern17() {
    sh tools/bundle.sh "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'K'; cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

libc20_run() {
    sh tools/bundle.sh stage017/libc20/include/*.h \
        "sys/time.h=stage017/libc20/include/sys/time.h" \
        "sys/stat.h=stage017/libc20/include/sys/stat.h" \
        "stage017/libc20/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l20_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l20_$2.i" \
        > "tmp/build/l20_$2.o"
    echo "built tmp/build/l20_$2.o" >&2
}

libc19_run() {
    sh tools/bundle.sh stage017/libc19/include/*.h \
        "sys/time.h=stage017/libc19/include/sys/time.h" \
        "sys/stat.h=stage017/libc19/include/sys/stat.h" \
        "stage017/libc19/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l19_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l19_$2.i" \
        > "tmp/build/l19_$2.o"
    echo "built tmp/build/l19_$2.o" >&2
}

# OS の上で動く実行形式を 1 つ作る。**libc19 と繋ぐ** 版
osprog19_run() {
    sh tools/bundle.sh stage017/libc19/include/*.h \
        "sys/stat.h=stage017/libc19/include/sys/stat.h" \
        "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'E'; cat "tmp/build/${1}.o" \
        tmp/build/l19_src_string.o tmp/build/l19_src_stdlib.o \
        tmp/build/l19_src_misc15.o tmp/build/l19_posix_sys.o \
        tmp/build/l19_posix_morecore.o tmp/build/l19_posix_stdio.o \
        tmp/build/l19_posix_assert.o tmp/build/l19_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
}

# pp17 は .sc なので前処理を通さない (tool1 と同じ道)。ただし 'E' で
# 組む —— sys_openat などのスタブは 'E' 前置部にしかないからである。
# フラットで組むと未定義で落ちる (実測 rc=7)
pp17_run() {
    { cat stage017/pp17.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15p.bin > tmp/build/pp17.o
    { printf 'E'; cat tmp/build/pp17.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/pp17
    echo "built tmp/build/pp17" >&2
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
    run_stage stage017 pp16cmd cc15pcmd cc15qcmd cc15rcmd ld16cmd cc17 cc18 cc19 ar17 pp17 mk17 mk18 mk19 mk20 stamp \
        kernel23.bin kernel24.bin \
        l19_src_string.o l19_src_ctype.o l19_src_stdlib.o \
        l19_src_morecore.o l19_src_misc15.o \
        l19_posix_sys.o l19_posix_morecore.o l19_posix_stdio.o \
        l19_posix_assert.o l19_posix_dir.o \
        l20_src_string.o l20_src_ctype.o l20_src_stdlib.o \
        l20_src_morecore.o l20_src_misc15.o \
        l20_posix_sys.o l20_posix_morecore.o l20_posix_stdio.o \
        l20_posix_assert.o l20_posix_dir.o \
        -- stage017/cc17.c stage017/cc18.c stage017/cc19.c stage017/ar17.c \
           stage017/pp17.sc \
           stage017/mk17.c stage017/mk18.c stage017/mk19.c \
           stage017/mk20.c \
           stage017/kernel23.c stage017/kernel24.c \
           tests/stage017/user/stamp.c \
           stage017/libc19/include/*.h stage017/libc19/include/sys/*.h \
           stage017/libc19/src/*.c stage017/libc19/posix/*.c \
           stage017/libc20/include/*.h stage017/libc20/include/sys/*.h \
           stage017/libc20/src/*.c stage017/libc20/posix/*.c \
           stage016/libc18/include/*.h \
           stage016/libc18/include/sys/*.h \
           tmp/build/stage016.stamp tools/build/stage017.sh tools/bundle.sh
}
