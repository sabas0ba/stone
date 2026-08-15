# stage015b のビルド手順と，入力・生成物の宣言。
#
# 第 6 部で足した最前線の道具 (cc15l〜cc15n・pp15・pp16・ld15)。
# 前段は stage015a の cc15k である。世代を足すときはここだけが
# 作り直される。

build_stage015b() {
    # 容量の世代 (第 6 部。tcc の翻訳単位が cc15k の器を溢れさせる)。
    # 前段は cc15k
    { cat stage015/cc15l.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15k.bin > tmp/build/cc15l0.o
    { cat tmp/build/cc15l0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15l0.bin
    echo "built tmp/build/cc15l0.bin (bootstrap)" >&2
    { cat stage015/cc15l.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15l0.bin > tmp/build/cc15l.o
    { cat tmp/build/cc15l.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15l.bin
    echo "built tmp/build/cc15l.bin" >&2
    # tcc が使う C の機能 (第 6 部その 2。docs/stage015-tcc.md 12.6)。
    # 前段は cc15l
    { cat stage015/cc15m.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15l.bin > tmp/build/cc15m0.o
    { cat tmp/build/cc15m0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15m0.bin
    echo "built tmp/build/cc15m0.bin (bootstrap)" >&2
    { cat stage015/cc15m.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15m0.bin > tmp/build/cc15m.o
    { cat tmp/build/cc15m.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15m.bin
    echo "built tmp/build/cc15m.bin" >&2
    # 整数定数の型 (C89 6.1.3.2。docs/stage015-tcc.md 12.14)。前段は cc15m
    { cat stage015/cc15n.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15m.bin > tmp/build/cc15n0.o
    { cat tmp/build/cc15n0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15n0.bin
    echo "built tmp/build/cc15n0.bin (bootstrap)" >&2
    { cat stage015/cc15n.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15n0.bin > tmp/build/cc15n.o
    { cat tmp/build/cc15n.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc15n.bin
    echo "built tmp/build/cc15n.bin" >&2
    # 同じく容量の世代の pp (マクロ表とアリーナ。docs/stage015-tcc.md 12.1)
    { cat stage015/pp15.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15n.bin > tmp/build/pp15.o
    { cat tmp/build/pp15.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp15.bin
    echo "built tmp/build/pp15.bin" >&2
    # 再帰抑止を直した pp (docs/stage015-tcc.md 12.7)
    { cat stage015/pp16.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15n.bin > tmp/build/pp16.o
    { cat tmp/build/pp16.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp16.bin
    echo "built tmp/build/pp16.bin" >&2
    # tcc の .o (約 1 MB・シンボル数千) を受けるリンカ (12.8)
    { cat stage015/ld15.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15n.bin > tmp/build/ld15.o
    { cat tmp/build/ld15.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld15.bin
    echo "built tmp/build/ld15.bin" >&2
}

do_stage015b() {
    run_stage stage015b cc15l0.bin cc15l.bin \
        cc15m0.bin cc15m.bin cc15n0.bin cc15n.bin \
        pp15.bin pp16.bin ld15.bin \
        -- stage015/cc15l.sc stage015/cc15m.sc stage015/cc15n.sc \
           stage015/pp15.sc stage015/pp16.sc stage015/ld15.sc \
           tmp/build/stage015a.stamp tools/build/stage015b.sh
}
