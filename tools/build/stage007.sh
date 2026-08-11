# stage007 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage007 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

build_stage007() {
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc.bin > tmp/build/occ1.bin
    echo "built tmp/build/occ1.bin" >&2
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ1.bin > tmp/build/occ.bin
    echo "built tmp/build/occ.bin" >&2
}

do_stage007() {
    run_stage stage007 occ1.bin occ.bin \
        -- stage007/occ.sc tmp/build/stage006.stamp tools/build/stage007.sh
}
