#!/bin/sh
# 生成物のビルド。成果物は tmp/build/ (git ignore) に置く。
# 各 Stage の成果物は前段の成果物のみでビルドする (docs/plan.md 2.1)。
#
# 使用法: build.sh [stage002|...|stage012|stage013|all]
#
# キャッシュ (スタンプ):
#   ビルドは決定的である (同じ入力から常に同じバイト列が生成される。
#   各 Stage のテストが SHA-256 の照合と固定点で保証している)。したがって
#   入力が前回と一致する Stage は作り直さなくてよい。
#   各 Stage の tmp/build/<stage>.stamp に「入力のハッシュ」と「生成物の
#   sha256sum」を記録し，両方が一致すればその Stage を省略する。
#   入力に前段のスタンプを含めることで，上流の変更は下流全体へ伝播する。
#   STONE_FORCE_BUILD=1 でスタンプを無視して作り直す (dev-notes.md 1.3)。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
mkdir -p tmp/build

# run_stage <stage> <生成物 (tmp/build/ 内の名前)...> -- <入力ファイル...>
run_stage() {
    name=$1; shift
    outs=""
    while [ "$1" != -- ]; do outs="$outs tmp/build/$1"; shift; done
    shift
    stamp=tmp/build/$name.stamp
    new=$(sha256sum "$@" | sha256sum | cut -d' ' -f1)
    if [ -z "${STONE_FORCE_BUILD:-}" ] && [ -f "$stamp" ] \
        && [ "$(head -n 1 "$stamp")" = "$new" ] \
        && tail -n +2 "$stamp" | sha256sum -c --status - 2>/dev/null; then
        echo "cached $name (stamp: 入力と生成物が前回と一致)" >&2
        return 0
    fi
    "build_$name"
    { echo "$new"; sha256sum $outs; } > "$stamp"
}

build_stage002() {
    sh tools/env.sh qemu stage001/hex0.bin < stage002/hex1.hex > tmp/build/hex1.bin
    echo "built tmp/build/hex1.bin" >&2
}

build_stage003() {
    sh tools/env.sh qemu tmp/build/hex1.bin < stage003/asm.hex1 > tmp/build/asm.bin
    echo "built tmp/build/asm.bin" >&2
}

build_stage004() {
    sh tools/env.sh qemu tmp/build/asm.bin < stage004/sol.s > tmp/build/sol.bin
    echo "built tmp/build/sol.bin" >&2
}

build_stage005() {
    sh tools/env.sh qemu tmp/build/sol.bin < stage005/sc.sol > tmp/build/sc.bin
    echo "built tmp/build/sc.bin" >&2
}

# sc 入力の終端は EOT (0x04)
build_stage006() {
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/sc.bin > tmp/build/scc1.bin
    echo "built tmp/build/scc1.bin" >&2
    { cat stage006/scc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc1.bin > tmp/build/scc.bin
    echo "built tmp/build/scc.bin" >&2
}

build_stage007() {
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/scc.bin > tmp/build/occ1.bin
    echo "built tmp/build/occ1.bin" >&2
    { cat stage007/occ.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ1.bin > tmp/build/occ.bin
    echo "built tmp/build/occ.bin" >&2
}

# Stage 8 以降は「コンパイル -> リンク」の 2 段になる。
# cc / ld 自身のブートストラップは occ (フラット出力) が担う。
# 生成物を cc8.bin と呼ぶのは，Stage 10 で後継の C コンパイラが出てくるため。
# cc.bin は常に最新世代を指し，過去世代は世代番号を付けて呼ぶ
# (docs/stage010-c89.md 2.1)。
build_stage008() {
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/cc0.bin
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/occ.bin > tmp/build/ld0.bin
    echo "built tmp/build/cc0.bin tmp/build/ld0.bin (bootstrap)" >&2
    { cat stage008/cc.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/cc8.o
    { cat tmp/build/cc8.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/cc8.bin
    echo "built tmp/build/cc8.bin" >&2
    { cat stage008/ld.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc0.bin > tmp/build/ld.o
    { cat tmp/build/ld.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld0.bin > tmp/build/ld.bin
    echo "built tmp/build/ld.bin" >&2
}

# Stage 9 は Stage 8 の cc + ld でビルドする。pp 自身は指令を含まないため，
# 前処理を通さずに直接コンパイルできる。前段の成果物のみでビルドするという
# 約束のとおり，ここは Stage 10 の cc ではなく cc8 を使う。
build_stage009() {
    { cat stage009/pp.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc8.bin > tmp/build/pp.o
    { cat tmp/build/pp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/pp.bin
    echo "built tmp/build/pp.bin" >&2
}

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

# Stage 12 は Stage 8 の ld でリンカの新世代 (ld12) を作り，pp + cc で
# カーネルを作る。カーネルは 'K' 形式 (フラット + カーネル前置部) で，
# ユーザプログラムは 'E' 形式 (ELF 実行形式) でリンクする
# (docs/stage012-os.md 5.3)
build_stage012() {
    { cat stage012/ld12.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/ld12.o
    { cat tmp/build/ld12.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld12.bin
    echo "built tmp/build/ld12.bin" >&2
    sh tools/bundle.sh stage012/kernel.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/kernel.i
    sh tools/env.sh qemu tmp/build/cc.bin < tmp/build/kernel.i > tmp/build/kernel.o
    { printf 'K'; cat tmp/build/kernel.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld12.bin > tmp/build/kernel.bin
    echo "built tmp/build/kernel.bin" >&2
    # 第 12 世代の libc。純粋部 (src) と環境部 (posix) を持つ
    for f in src/string src/ctype src/stdlib src/morecore posix/sys posix/morecore posix/stdio; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage012/libc/include/*.h "stage012/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l12_$n.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/l12_$n.i" > "tmp/build/l12_$n.o"
        echo "built tmp/build/l12_$n.o" >&2
    done
}

# Stage 13 はリンカの新世代 (ld13 = ld12 + sys_ecall) を Stage 8 の ld で，
# カーネル (kernel13 = spawn とつなぎ替え) とシェル (sh13) を pp + cc +
# ld13 で作る (docs/stage013-tools.md 3 章)。libc は第 13 世代
# (stage013/libc。spawn の包みを足した) をオブジェクトのまま置く
build_stage013() {
    { cat stage013/ld13.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc.bin > tmp/build/ld13.o
    { cat tmp/build/ld13.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld.bin > tmp/build/ld13.bin
    echo "built tmp/build/ld13.bin" >&2
    sh tools/bundle.sh stage013/kernel.c \
        | sh tools/env.sh qemu tmp/build/pp.bin > tmp/build/kernel13.i
    sh tools/env.sh qemu tmp/build/cc.bin < tmp/build/kernel13.i > tmp/build/kernel13.o
    { printf 'K'; cat tmp/build/kernel13.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld13.bin > tmp/build/kernel13.bin
    echo "built tmp/build/kernel13.bin" >&2
    # 第 13 世代の libc
    for f in src/string src/ctype src/stdlib src/morecore posix/sys posix/morecore posix/stdio; do
        n=$(echo "$f" | tr / _)
        sh tools/bundle.sh stage013/libc/include/*.h "stage013/libc/$f.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l13_$n.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/l13_$n.i" > "tmp/build/l13_$n.o"
        echo "built tmp/build/l13_$n.o" >&2
    done
    # OS 上の道具 (ELF 実行形式)。どれも libc の第 13 世代を並べる
    for t in sh ed bundle ldin eot mk; do
        sh tools/bundle.sh stage013/libc/include/*.h "stage013/$t.c" \
            | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/${t}13.i"
        sh tools/env.sh qemu tmp/build/cc.bin < "tmp/build/${t}13.i" > "tmp/build/${t}13.o"
        { printf 'E'; cat "tmp/build/${t}13.o" tmp/build/l13_src_string.o \
            tmp/build/l13_src_stdlib.o tmp/build/l13_posix_sys.o \
            tmp/build/l13_posix_morecore.o tmp/build/l13_posix_stdio.o; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld13.bin > "tmp/build/${t}13"
        echo "built tmp/build/${t}13" >&2
    done
    # 処理系そのものを OS 上のコマンドへ移す (docs/stage013-tools.md 7 章)。
    # ソースは無く，既にある .o を 'E' でリンクし直すだけである。
    # 'E' 前置部の getc / putc が read(0) / write(1) の 1 バイト版なので，
    # 「標準入力を読み標準出力へ書くフィルタ」という姿がそのまま通じる
    for pair in pp:pp cc:cc10l ld:ld13; do
        t=${pair%%:*}
        o=${pair##*:}
        { printf 'E'; cat "tmp/build/$o.o"; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld13.bin > "tmp/build/${t}13cmd"
        echo "built tmp/build/${t}13cmd" >&2
    done
}

# 各 Stage の入力 (ソースと前段のスタンプ) と生成物の宣言。
# 生成物には後段とテストが参照するファイルをすべて挙げる (.o / .i の
# 中間物は挙げない。スタンプはそれらの有無を保証しない)。
# tools/build.sh 自身を入力に含めるのは，ビルド手順の変更で作り直すためである
do_stage002() {
    run_stage stage002 hex1.bin \
        -- stage001/hex0.bin stage002/hex1.hex tools/build.sh
}
do_stage003() {
    run_stage stage003 asm.bin \
        -- stage003/asm.hex1 tmp/build/stage002.stamp tools/build.sh
}
do_stage004() {
    run_stage stage004 sol.bin \
        -- stage004/sol.s tmp/build/stage003.stamp tools/build.sh
}
do_stage005() {
    run_stage stage005 sc.bin \
        -- stage005/sc.sol tmp/build/stage004.stamp tools/build.sh
}
do_stage006() {
    run_stage stage006 scc1.bin scc.bin \
        -- stage006/scc.sc tmp/build/stage005.stamp tools/build.sh
}
do_stage007() {
    run_stage stage007 occ1.bin occ.bin \
        -- stage007/occ.sc tmp/build/stage006.stamp tools/build.sh
}
do_stage008() {
    run_stage stage008 cc0.bin ld0.bin cc8.bin ld.bin \
        -- stage008/cc.sc stage008/ld.sc tmp/build/stage007.stamp tools/build.sh
}
do_stage009() {
    run_stage stage009 pp.bin \
        -- stage009/pp.sc tmp/build/stage008.stamp tools/build.sh
}
# Stage 10 が使うのは cc8 と ld (Stage 8 の成果物) であり pp ではないので，
# 前段のスタンプは stage008 を指す
do_stage010() {
    run_stage stage010 \
        cc10a0.bin cc10a.bin cc10b.bin cc10c0.bin cc10c.bin \
        cc10d0.bin cc10d.bin cc10e0.bin cc10e.bin cc10f0.bin cc10f.bin \
        cc10g0.bin cc10g.bin cc10h0.bin cc10h.bin cc10i0.bin cc10i.bin \
        cc10j0.bin cc10j.bin cc10k0.bin cc10k.bin cc10l0.bin cc10l.bin \
        cc.bin \
        -- stage010/*.sc stage010/include/*.h tmp/build/stage008.stamp tools/build.sh
}
do_stage011() {
    run_stage stage011 l11_string.o l11_ctype.o l11_stdlib.o \
        -- stage011/libc/include/*.h stage011/libc/src/*.c \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tools/build.sh tools/bundle.sh
}
do_stage012() {
    run_stage stage012 ld12.bin kernel.bin l12_src_string.o l12_src_ctype.o \
        l12_src_stdlib.o l12_src_morecore.o l12_posix_sys.o l12_posix_morecore.o \
        l12_posix_stdio.o \
        -- stage012/ld12.sc stage012/kernel.c stage012/libc/include/*.h \
           stage012/libc/src/*.c stage012/libc/posix/*.c tmp/build/stage008.stamp \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tmp/build/stage011.stamp tools/build.sh tools/bundle.sh
}
do_stage013() {
    run_stage stage013 ld13.bin kernel13.bin l13_src_string.o l13_src_ctype.o \
        l13_src_stdlib.o l13_src_morecore.o l13_posix_sys.o l13_posix_morecore.o \
        l13_posix_stdio.o sh13 ed13 bundle13 ldin13 eot13 mk13 \
        pp13cmd cc13cmd ld13cmd \
        -- stage013/ld13.sc stage013/kernel.c stage013/sh.c stage013/ed.c \
           stage013/bundle.c stage013/ldin.c stage013/eot.c stage013/mk.c \
           stage013/libc/include/*.h stage013/libc/src/*.c \
           stage013/libc/posix/*.c tmp/build/stage008.stamp \
           tmp/build/stage009.stamp tmp/build/stage010.stamp \
           tools/build.sh tools/bundle.sh
}

stages="stage002 stage003 stage004 stage005 stage006 stage007 stage008 stage009 stage010 stage011 stage012 stage013"
target=${1:-all}
[ "$target" = all ] && target=stage013
case " $stages " in
*" $target "*) ;;
*)
    echo "usage: build.sh [stage002|...|stage012|stage013|all]" >&2
    exit 2
    ;;
esac
for s in $stages; do
    "do_$s"
    [ "$s" = "$target" ] && break
done
exit 0
