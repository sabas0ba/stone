# stage002 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage002 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

build_stage002() {
    sh tools/env.sh qemu stage001/hex0.bin < stage002/hex1.hex > tmp/build/hex1.bin
    echo "built tmp/build/hex1.bin" >&2
}

# 各 Stage の入力 (ソースと前段のスタンプ) と生成物の宣言。
# 生成物には後段とテストが参照するファイルをすべて挙げる (.o / .i の
# 中間物は挙げない。スタンプはそれらの有無を保証しない)。
# tools/build/stage002.sh 自身を入力に含めるのは，ビルド手順の変更で作り直すためである
do_stage002() {
    run_stage stage002 hex1.bin \
        -- stage001/hex0.bin stage002/hex1.hex tools/build/stage002.sh
}
