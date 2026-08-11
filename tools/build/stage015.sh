# stage015 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage015 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 15 は cc の新世代 (cc15a = 64 bit 整数の土台) を Stage 14 の
# 最前線 cc14g で作る (docs/stage015-tcc.md 6 章)
build_stage015() {
    { cat stage015/cc15a.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14g.bin > tmp/build/cc15a0.o
    { cat tmp/build/cc15a0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15a0.bin
    echo "built tmp/build/cc15a0.bin (bootstrap)" >&2
    { cat stage015/cc15a.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15a0.bin > tmp/build/cc15a.o
    { cat tmp/build/cc15a.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15a.bin
    echo "built tmp/build/cc15a.bin" >&2
    # 64 bit の演算 (第 2 部の後半)。前段は cc15a
    { cat stage015/cc15b.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15a.bin > tmp/build/cc15b0.o
    { cat tmp/build/cc15b0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15b0.bin
    echo "built tmp/build/cc15b0.bin (bootstrap)" >&2
    { cat stage015/cc15b.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15b0.bin > tmp/build/cc15b.o
    { cat tmp/build/cc15b.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15b.bin
    echo "built tmp/build/cc15b.bin" >&2
    # 64 bit の引数 (第 2 部の続き)。前段は cc15b
    { cat stage015/cc15c.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15b.bin > tmp/build/cc15c0.o
    { cat tmp/build/cc15c0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15c0.bin
    echo "built tmp/build/cc15c0.bin (bootstrap)" >&2
    { cat stage015/cc15c.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15c0.bin > tmp/build/cc15c.o
    { cat tmp/build/cc15c.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15c.bin
    echo "built tmp/build/cc15c.bin" >&2
    # 64 bit の返却と乗算 (第 2 部の続き)。前段は cc15c
    { cat stage015/cc15d.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15c.bin > tmp/build/cc15d0.o
    { cat tmp/build/cc15d0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15d0.bin
    echo "built tmp/build/cc15d0.bin (bootstrap)" >&2
    { cat stage015/cc15d.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15d0.bin > tmp/build/cc15d.o
    { cat tmp/build/cc15d.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15d.bin
    echo "built tmp/build/cc15d.bin" >&2
    # 64 bit の除算 (第 2 部の締め)。前段は cc15d
    { cat stage015/cc15e.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15d.bin > tmp/build/cc15e0.o
    { cat tmp/build/cc15e0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15e0.bin
    echo "built tmp/build/cc15e0.bin (bootstrap)" >&2
    { cat stage015/cc15e.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15e0.bin > tmp/build/cc15e.o
    { cat tmp/build/cc15e.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15e.bin
    echo "built tmp/build/cc15e.bin" >&2
    # 実行時支援 (64 bit の除算の実体)。cc15e 自身でコンパイルする。
    # ブロックコメントを含むので pp を通す (cc は // しか解さない)
    sh tools/bundle.sh stage015/rt64.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/rt64.i
    sh tools/env.sh qemu tmp/build/cc15e.bin < tmp/build/rt64.i > tmp/build/rt64.o
    echo "built tmp/build/rt64.o" >&2
}

do_stage015() {
    run_stage stage015 cc15a0.bin cc15a.bin cc15b0.bin cc15b.bin \
        cc15c0.bin cc15c.bin cc15d0.bin cc15d.bin \
        cc15e0.bin cc15e.bin rt64.o \
        -- stage015/cc15a.sc stage015/cc15b.sc stage015/cc15c.sc \
           stage015/cc15d.sc stage015/cc15e.sc stage015/rt64.c \
           tmp/build/stage014.stamp tools/build/stage015.sh
}
