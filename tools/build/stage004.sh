# stage004 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage004 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

build_stage004() {
    sh tools/env.sh qemu tmp/build/asm.bin < stage004/sol.s > tmp/build/sol.bin
    echo "built tmp/build/sol.bin" >&2
}

do_stage004() {
    run_stage stage004 sol.bin \
        -- stage004/sol.s tmp/build/stage003.stamp tools/build/stage004.sh
}
