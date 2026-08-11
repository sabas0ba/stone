# stage009 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage009 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 9 は Stage 8 の cc + ld でビルドする。pp 自身は指令を含まないため，
# 前処理を通さずに直接コンパイルできる。前段の成果物のみでビルドするという
# 約束のとおり，ここは Stage 10 の cc ではなく cc8 を使う。
build_stage009() {
    { cat stage009/pp.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc8.bin > tmp/build/pp.o
    { cat tmp/build/pp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp.bin
    echo "built tmp/build/pp.bin" >&2
}

do_stage009() {
    run_stage stage009 pp.bin \
        -- stage009/pp.sc tmp/build/stage008.stamp tools/build/stage009.sh
}
