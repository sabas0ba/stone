# stage014 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage014 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 14 は cc の新世代 (cc14a) を作る。前段の成果物のみでビルドする
# 約束のとおり，1 段目は cc10l (= cc.bin) が作り，正本はその 1 段目が
# 自分自身を再コンパイルしたものである (以降は固定点)。
#
# **cc.bin は差し替えない。** cc.bin を最新世代へ向けると Stage 11 以降の
# 記録済み成果物がすべて別のバイト列になる (それらは cc10l が作ったもの
# として凍結されている。docs/artifacts.md)。世代を足す段では別名で置き，
# 差し替えは必要になった時点で判断する (docs/stage014-external.md 5.3)
build_stage014() {
    { cat stage014/cc14.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/cc14a0.o
    { cat tmp/build/cc14a0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14a0.bin
    echo "built tmp/build/cc14a0.bin (bootstrap)" >&2
    { cat stage014/cc14.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14a0.bin > tmp/build/cc14a.o
    { cat tmp/build/cc14a.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14a.bin
    echo "built tmp/build/cc14a.bin" >&2
    # 第 3 部 (入れ子の初期化子)。前段は cc14a
    { cat stage014/cc14b.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14a.bin > tmp/build/cc14b0.o
    { cat tmp/build/cc14b0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14b0.bin
    echo "built tmp/build/cc14b0.bin (bootstrap)" >&2
    { cat stage014/cc14b.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14b0.bin > tmp/build/cc14b.o
    { cat tmp/build/cc14b.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14b.bin
    echo "built tmp/build/cc14b.bin" >&2
    # 第 4 部 (関数内 static と構造体メンバの関数ポインタ)。前段は cc14b
    { cat stage014/cc14c.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14b.bin > tmp/build/cc14c0.o
    { cat tmp/build/cc14c0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14c0.bin
    echo "built tmp/build/cc14c0.bin (bootstrap)" >&2
    { cat stage014/cc14c.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14c0.bin > tmp/build/cc14c.o
    { cat tmp/build/cc14c.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14c.bin
    echo "built tmp/build/cc14c.bin" >&2
    # 第 5 部 (typedef の前方参照と K&R 形式)。前段は cc14c
    { cat stage014/cc14d.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14c.bin > tmp/build/cc14d0.o
    { cat tmp/build/cc14d0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14d0.bin
    echo "built tmp/build/cc14d0.bin (bootstrap)" >&2
    { cat stage014/cc14d.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14d0.bin > tmp/build/cc14d.o
    { cat tmp/build/cc14d.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14d.bin
    echo "built tmp/build/cc14d.bin" >&2
    # 第 6 部 (ビットフィールド)。前段は cc14d
    { cat stage014/cc14e.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14d.bin > tmp/build/cc14e0.o
    { cat tmp/build/cc14e0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14e0.bin
    echo "built tmp/build/cc14e0.bin (bootstrap)" >&2
    { cat stage014/cc14e.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14e0.bin > tmp/build/cc14e.o
    { cat tmp/build/cc14e.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14e.bin
    echo "built tmp/build/cc14e.bin" >&2
    # 第 8 部 (外部ソース bzip2)。前段は cc14e
    { cat stage014/cc14f.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14e.bin > tmp/build/cc14f0.o
    { cat tmp/build/cc14f0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14f0.bin
    echo "built tmp/build/cc14f0.bin (bootstrap)" >&2
    { cat stage014/cc14f.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14f0.bin > tmp/build/cc14f.o
    { cat tmp/build/cc14f.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14f.bin
    echo "built tmp/build/cc14f.bin" >&2
    # リンカの第 14 世代 (シンボル名 31 バイト)。ld13 と同じ道具立てで作る
    { cat stage014/ld14.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/ld14.o
    { cat tmp/build/ld14.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld14.bin
    echo "built tmp/build/ld14.bin" >&2
    # 第 9 部 (zlib)。前段は cc14f
    { cat stage014/cc14g.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14f.bin > tmp/build/cc14g0.o
    { cat tmp/build/cc14g0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14g0.bin
    echo "built tmp/build/cc14g0.bin (bootstrap)" >&2
    { cat stage014/cc14g.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14g0.bin > tmp/build/cc14g.o
    { cat tmp/build/cc14g.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc14g.bin
    echo "built tmp/build/cc14g.bin" >&2
    # pp の第 14 世代 (容量拡大。第 9 部)。同じ道具立てで作る
    { cat stage014/pp14.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/pp14.o
    { cat tmp/build/pp14.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp14.bin
    echo "built tmp/build/pp14.bin" >&2
    # 第 14 世代の libc (assert・printf の拡張・sprintf)。最前線の cc14g で
    # コンパイルする (外部ソースと同じ経路に載せるため)
    for f in src/string src/ctype src/stdlib src/morecore posix/sys posix/morecore posix/stdio posix/assert; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage014/libc/include/*.h "stage014/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l14_$n.i"
        sh tools/env.sh qemu tmp/build/cc14g.bin < "tmp/build/l14_$n.i" > "tmp/build/l14_$n.o"
        echo "built tmp/build/l14_$n.o" >&2
    done
}

do_stage014() {
    run_stage stage014 cc14a0.bin cc14a.bin cc14b0.bin cc14b.bin \
        cc14c0.bin cc14c.bin cc14d0.bin cc14d.bin cc14e0.bin cc14e.bin \
        cc14f0.bin cc14f.bin cc14g0.bin cc14g.bin ld14.bin pp14.bin \
        l14_src_string.o l14_src_ctype.o l14_src_stdlib.o l14_src_morecore.o \
        l14_posix_sys.o l14_posix_morecore.o l14_posix_stdio.o l14_posix_assert.o \
        -- stage014/cc14.sc stage014/cc14b.sc stage014/cc14c.sc \
           stage014/cc14d.sc stage014/cc14e.sc stage014/cc14f.sc \
           stage014/cc14g.sc stage014/ld14.sc stage014/pp14.sc \
           stage014/libc/include/*.h \
           stage014/libc/src/*.c stage014/libc/posix/*.c \
           tmp/build/stage010.stamp tools/build/stage014.sh tools/bundle.sh
}
