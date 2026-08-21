# stage010 のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身が stage010 の
# スタンプの入力である** ので，ここを直せばこの Stage だけが作り直される
# (build.sh 全体を入力にしていた頃は，どの Stage を触っても全段が
# 作り直しになっていた)。

# Stage 10 は 3 部に分かれ，各部のコンパイラが次の部をビルドする。
# 生成物は世代ごとに名前を持ち (cc10a = 第 1 部, cc10b = 第 2 部)，
# 最新世代を cc.bin として複製する (docs/stage010-c89.md 2.1)。
#
#   cc8   -> cc10a0 -> cc10a  (第 1 部。cc10a0 == cc10a)
#   cc10a -> cc10b           (第 2 部)
build_stage010() {
    { cat stage010/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc8.bin > tmp/build/cc10a0.o
    { cat tmp/build/cc10a0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10a0.bin
    echo "built tmp/build/cc10a0.bin (bootstrap)" >&2
    { cat stage010/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10a0.bin > tmp/build/cc10a.o
    { cat tmp/build/cc10a.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10a.bin
    echo "built tmp/build/cc10a.bin" >&2
    { cat stage010/cc2.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10a.bin > tmp/build/cc10b.o
    { cat tmp/build/cc10b.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10b.bin
    echo "built tmp/build/cc10b.bin" >&2
    # 第 2 部の 2 はフレームの割付け方を変えるので，1 段目 (cc10b が作ったもの)
    # と正本は一致しない。正本はその 1 段目が自分自身を再コンパイルしたもので，
    # 以降は固定点になる (B2 == B3)
    { cat stage010/cc3.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10b.bin > tmp/build/cc10c0.o
    { cat tmp/build/cc10c0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10c0.bin
    echo "built tmp/build/cc10c0.bin (bootstrap)" >&2
    { cat stage010/cc3.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10c0.bin > tmp/build/cc10c.o
    { cat tmp/build/cc10c.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10c.bin
    echo "built tmp/build/cc10c.bin" >&2
    { cat stage010/cc4.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10c.bin > tmp/build/cc10d0.o
    { cat tmp/build/cc10d0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10d0.bin
    echo "built tmp/build/cc10d0.bin (bootstrap)" >&2
    { cat stage010/cc4.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10d0.bin > tmp/build/cc10d.o
    { cat tmp/build/cc10d.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10d.bin
    echo "built tmp/build/cc10d.bin" >&2
    { cat stage010/cc5.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10d.bin > tmp/build/cc10e0.o
    { cat tmp/build/cc10e0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10e0.bin
    echo "built tmp/build/cc10e0.bin (bootstrap)" >&2
    { cat stage010/cc5.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10e0.bin > tmp/build/cc10e.o
    { cat tmp/build/cc10e.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10e.bin
    echo "built tmp/build/cc10e.bin" >&2
    { cat stage010/cc6.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10e.bin > tmp/build/cc10f0.o
    { cat tmp/build/cc10f0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10f0.bin
    echo "built tmp/build/cc10f0.bin (bootstrap)" >&2
    { cat stage010/cc6.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10f0.bin > tmp/build/cc10f.o
    { cat tmp/build/cc10f.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10f.bin
    echo "built tmp/build/cc10f.bin" >&2
    { cat stage010/cc7.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10f.bin > tmp/build/cc10g0.o
    { cat tmp/build/cc10g0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10g0.bin
    echo "built tmp/build/cc10g0.bin (bootstrap)" >&2
    { cat stage010/cc7.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10g0.bin > tmp/build/cc10g.o
    { cat tmp/build/cc10g.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10g.bin
    echo "built tmp/build/cc10g.bin" >&2
    { cat stage010/cc8.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10g.bin > tmp/build/cc10h0.o
    { cat tmp/build/cc10h0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10h0.bin
    echo "built tmp/build/cc10h0.bin (bootstrap)" >&2
    { cat stage010/cc8.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10h0.bin > tmp/build/cc10h.o
    { cat tmp/build/cc10h.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10h.bin
    echo "built tmp/build/cc10h.bin" >&2
    { cat stage010/cc9.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10h.bin > tmp/build/cc10i0.o
    { cat tmp/build/cc10i0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10i0.bin
    echo "built tmp/build/cc10i0.bin (bootstrap)" >&2
    { cat stage010/cc9.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10i0.bin > tmp/build/cc10i.o
    { cat tmp/build/cc10i.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10i.bin
    echo "built tmp/build/cc10i.bin" >&2
    { cat stage010/cc10.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10i.bin > tmp/build/cc10j0.o
    { cat tmp/build/cc10j0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10j0.bin
    echo "built tmp/build/cc10j0.bin (bootstrap)" >&2
    { cat stage010/cc10.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10j0.bin > tmp/build/cc10j.o
    { cat tmp/build/cc10j.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10j.bin
    echo "built tmp/build/cc10j.bin" >&2
    { cat stage010/cc11.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10j.bin > tmp/build/cc10k0.o
    { cat tmp/build/cc10k0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10k0.bin
    echo "built tmp/build/cc10k0.bin (bootstrap)" >&2
    { cat stage010/cc11.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10k0.bin > tmp/build/cc10k.o
    { cat tmp/build/cc10k.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10k.bin
    echo "built tmp/build/cc10k.bin" >&2
    { cat stage010/cc12.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10k.bin > tmp/build/cc10l0.o
    { cat tmp/build/cc10l0.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10l0.bin
    echo "built tmp/build/cc10l0.bin (bootstrap)" >&2
    { cat stage010/cc12.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc10l0.bin > tmp/build/cc10l.o
    { cat tmp/build/cc10l.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/cc10l.bin
    echo "built tmp/build/cc10l.bin" >&2
    # cc.bin は常に最新世代を指す別名
    cp tmp/build/cc10l.bin tmp/build/cc.bin
    echo "built tmp/build/cc.bin (= cc10l.bin)" >&2
}

# Stage 10 が使うのは cc8 と ld (Stage 8 の成果物) であり pp ではないので，
# 前段のスタンプは stage008 を指す
do_stage010() {
    run_stage stage010 \
        cc10a0.bin cc10a.bin cc10b.bin cc10c0.bin cc10c.bin \
        cc10d0.bin cc10d.bin cc10e0.bin cc10e.bin cc10f0.bin cc10f.bin \
        cc10g0.bin cc10g.bin cc10h0.bin cc10h.bin cc10i0.bin cc10i.bin \
        cc10j0.bin cc10j.bin cc10k0.bin cc10k.bin cc10l0.bin cc10l.bin \
        cc.bin cc10l.o \
        -- stage010/*.sc stage010/include/*.h tmp/build/stage008.stamp tools/build/stage010.sh
}
