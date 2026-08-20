# stage016 のビルド手順と，入力・生成物の宣言。
#
# 第 1 部はファイル系 (sfs2)。カーネルの新世代 kernel17 が sfs2 を読み，
# 経路をディレクトリの木として解決する (docs/stage016-os.md 6 章)。
# 最前線の cc15p / pp16 / ld16 で作る。

build_stage016() {
    sh tools/bundle.sh stage016/kernel17.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/kernel17.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/kernel17.i \
        > tmp/build/kernel17.o
    { printf 'K'; cat tmp/build/kernel17.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/kernel17.bin
    echo "built tmp/build/kernel17.bin" >&2
}

do_stage016() {
    run_stage stage016 kernel17.bin \
        -- stage016/kernel17.c \
           tmp/build/stage015c.stamp tools/build/stage016.sh
}
