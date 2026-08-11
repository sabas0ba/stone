# stage003 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage003 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

build_stage003() {
    sh tools/env.sh qemu tmp/build/hex1.bin < stage003/asm.hex1 > tmp/build/asm.bin
    echo "built tmp/build/asm.bin" >&2
}

do_stage003() {
    run_stage stage003 asm.bin \
        -- stage003/asm.hex1 tmp/build/stage002.stamp tools/build/stage003.sh
}
