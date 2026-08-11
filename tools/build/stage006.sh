# stage006 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage006 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# sc 入力の終端は EOT (0x04)
build_stage006() {
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/sc.bin > tmp/build/scc1.bin
    echo "built tmp/build/scc1.bin" >&2
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc1.bin > tmp/build/scc.bin
    echo "built tmp/build/scc.bin" >&2
}

do_stage006() {
    run_stage stage006 scc1.bin scc.bin \
        -- stage006/scc.sc tmp/build/stage005.stamp tools/build/stage006.sh
}
