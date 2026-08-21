# stage008 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage008 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 8 以降は「コンパイル -> リンク」の 2 段になる。
# cc / ld 自身のブートストラップは occ (フラット出力) が担う。
# 生成物を cc8.bin と呼ぶのは，Stage 10 で後継の C コンパイラが出てくるため。
# cc.bin は常に最新世代を指し，過去世代は世代番号を付けて呼ぶ
# (docs/stage010-c89.md 2.1)。
build_stage008() {
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/cc0.bin
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/ld0.bin
    echo "built tmp/build/cc0.bin tmp/build/ld0.bin (bootstrap)" >&2
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/cc8.o
    { cat tmp/build/cc8.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/cc8.bin
    echo "built tmp/build/cc8.bin" >&2
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/ld.o
    { cat tmp/build/ld.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/ld.bin
    echo "built tmp/build/ld.bin" >&2
}

do_stage008() {
    run_stage stage008 cc0.bin ld0.bin cc8.bin ld.bin ld.o \
        -- stage008/cc.sc stage008/ld.sc tmp/build/stage007.stamp tools/build/stage008.sh
}
