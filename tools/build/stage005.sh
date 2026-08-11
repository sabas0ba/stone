# stage005 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage005 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

build_stage005() {
    sh tools/env.sh qemu tmp/build/sol.bin < stage005/sc.sol > tmp/build/sc.bin
    echo "built tmp/build/sc.bin" >&2
}

do_stage005() {
    run_stage stage005 sc.bin \
        -- stage005/sc.sol tmp/build/stage004.stamp tools/build/stage005.sh
}
