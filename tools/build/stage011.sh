# stage011 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage011 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 11 は Stage 10 の成果物 (pp + cc) でビルドする。libc はリンク済みの
# 実行像ではなくオブジェクトのまま置き，利用者が必要なものだけ ld へ並べる
# (docs/stage011-libc.md 2.2)。ヘッダは束ねで pp へ渡す (docs/stage009-pp.md 2.2)
build_stage011() {
    # 第 11 世代の libc (フリースタンディングのみ)。stage011/libc/ に凍結して
    # あり，後の世代が触ることはない (docs/roadmap.md 4.1)。
    # 生成物は世代の番号を前置して呼ぶ
    for f in string ctype stdlib; do
        sh tools/bundle.sh stage011/libc/include/*.h "stage011/libc/src/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l11_$f.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/l11_$f.i" > "tmp/build/l11_$f.o"
        echo "built tmp/build/l11_$f.o" >&2
    done
}

do_stage011() {
    run_stage stage011 l11_string.o l11_ctype.o l11_stdlib.o \
        -- stage011/libc/include/*.h stage011/libc/src/*.c \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tools/build/stage011.sh tools/bundle.sh
}
