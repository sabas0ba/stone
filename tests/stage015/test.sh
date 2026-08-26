#!/bin/bash
# Stage 15 テスト: 64 bit 整数の土台 (docs/stage015-tcc.md 6 章)。
#
# probe/ の各ソースを pp -> cc15a -> ld に通し，結果を ledger.txt と
# 突き合わせる。表と実測が食い違えば失敗する (Stage 14 と同じ枠組み)。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s15
stable_dir=tmp/s15/stable

cc=tmp/build/cc15r.bin   # 台帳は最前線の世代で測る
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage015/probe
hdr=stage015/libc/include/stdarg.h   # va_arg が語数ぶん進む版 (cc15k の 2 語の可変部に要る)

ensure_build stage015

probe() {
    n=$1
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s15/$n.i" 2> /dev/null || {
        echo "ppfail"
        return
    }
    sh tools/env.sh qemu "$cc" < "tmp/s15/$n.i" > "tmp/s15/$n.o" 2> /dev/null
    ccrc=$?
    if [ "$ccrc" -ne 0 ]; then
        echo "gap $ccrc"
        return
    fi
    # 実行時支援を必ず並べる。64 bit の除算と浮動小数点の変換はこれを呼ぶ
    { cat "tmp/s15/$n.o" tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > "tmp/s15/$n.bin" 2> /dev/null || {
        echo "linkfail"
        return
    }
    out=$(sh tools/env.sh qemu "tmp/s15/$n.bin" < /dev/null 2> /dev/null)
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "runfail $rc"
        return
    fi
    printf 'ok %s\n' "$(printf '%s\n' "$out" | sed -e 's/\\/\\\\/g' -e 's/\t/\\t/g' | tr '\n' '@' | sed 's/@/\\n/g')"
}

section "ビルド再現と固定点"

ok=0
for pair in cc15a.bin:stage015/cc15a.md cc15b.bin:stage015/cc15b.md \
        cc15c.bin:stage015/cc15c.md cc15d.bin:stage015/cc15d.md \
        cc15e.bin:stage015/cc15e.md cc15f.bin:stage015/cc15f.md \
        cc15g.bin:stage015/cc15g.md cc15h.bin:stage015/cc15h.md \
        cc15i.bin:stage015/cc15i.md cc15j.bin:stage015/cc15j.md \
        cc15k.bin:stage015/cc15k.md cc15l.bin:stage015/cc15l.md \
        cc15m.bin:stage015/cc15m.md cc15n.bin:stage015/cc15n.md \
        cc15o.bin:stage015/cc15o.md cc15p.bin:stage015/cc15p.md \
        cc15q.bin:stage015/cc15q.md cc15r.bin:stage015/cc15r.md \
        pp15.bin:stage015/pp15.md \
        pp16.bin:stage015/pp16.md ld16.bin:stage015/ld16.md; do
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "${pair##*:}" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/${pair%%:*}"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc15a..cc15r と pp15 / pp16 / ld16 の SHA-256 が各 .md 記載値と一致"

# **落ちたときに「中身が違う」のか「実行が再現していない」のかを
# 分ける** (1.6)。この検査は CI で実際に揺らいだ
fp15gen() {
    { cat stage015/cc15q.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15q.bin > tmp/s15/b3.o \
        && { cat tmp/s15/b3.o; printf '\0'; } \
            | sh tools/env.sh qemu "$ld" > "$1"
}
stable_cmp "fixpoint(cc15q)" fp15gen tmp/build/cc15q.bin
report $? "fixpoint: cc15q が自分自身を再生成する (B2 == B3)"

# **最前線の世代も同じ検査を通す。** cc15r はコード生成を変えている
# (文字列リテラルの sizeof) ので，roadmap 4 章の「コード生成を変える
# たびに B2 == B3 を確認する」がそのまま当たる。
# cc15q のときと同じ形で書く —— 世代を足して検査を足さないと、
# **足した世代だけ誰も見ていないことになる**
fp15rgen() {
    { cat stage015/cc15r.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15r.bin > tmp/s15/b3r.o \
        && { cat tmp/s15/b3r.o; printf '\0'; } \
            | sh tools/env.sh qemu "$ld" > "$1"
}
stable_cmp "fixpoint(cc15r)" fp15rgen tmp/build/cc15r.bin
report $? "fixpoint: cc15r が自分自身を再生成する (B2 == B3)"

# 64 bit を足しただけで，32 bit のコード生成は変えていない
ok=0
for n in sh ed mk; do
    sh tools/bundle.sh stage013/libc/include/*.h "stage013/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s15/r_$n.i" 2> /dev/null \
        && sh tools/env.sh qemu "$cc" < "tmp/s15/r_$n.i" > "tmp/s15/r_$n.o" 2> /dev/null \
        && cmp -s "tmp/s15/r_$n.o" "tmp/build/${n}13.o" || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: cc15r が既存のソース (sh / ed / mk) を cc10l と同じ .o にする"

section "適合台帳の照合 (64 bit の土台)"

nok=0
ngap=0
nbad=0
while read -r name state want rest; do
    case "$name" in ''|'#'*) continue ;; esac
    got=$(probe "$name")
    gs=${got%% *}
    gv=${got#* }
    if [ "$state" = gap ]; then
        [ "$gs" = gap ] && [ "$gv" = "$want" ]
        r=$?
        report $r "gap: $name (cc が $want で拒む) ${rest:-}"
        ngap=$((ngap + 1))
    else
        [ "$gs" = ok ] && [ "$gv" = "$want" ]
        r=$?
        if [ "$state" = bad ]; then
            report $r "bad: $name (通るが誤り) ${rest:-}"
            nbad=$((nbad + 1))
        else
            report $r "ok:  $name"
            nok=$((nok + 1))
        fi
    fi
done < tests/stage015/ledger.txt

echo
echo "   台帳: 通る $nok 件 / 未対応 $ngap 件 / 通るが誤り $nbad 件"

# ---------------------------------------------------------------------------
section "第 3 部: 浮動小数点の実行時支援 (docs/stage015-tcc.md 10 章)"

# rtfp.c は浮動小数点の型を使わずに書いてあるので，第 2 部までの cc で
# 通る。ここでは**実行時支援そのもの**を単体で確かめる。処理系が float /
# double を読めるようにするのは後続の世代 (cc15f 以降)
sh tools/bundle.sh "$hdr" tests/stage015/probe/fpsoft.c 2> /dev/null \
    | sh tools/env.sh qemu "$pp" > tmp/s15/fpsoft.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/fpsoft.i > tmp/s15/fpsoft.o 2> /dev/null
report $? "fp: rtfp.c の検査ソースがコンパイルできる"

{ cat tmp/s15/fpsoft.o tmp/build/rtfp.o; printf '\0'; } \
    | sh tools/env.sh qemu "$ld" > tmp/s15/fpsoft.bin 2> /dev/null
report $? "fp: 実行時支援とリンクできる"

# 期待値はホストの double から作った表。加減乗除を 21 組ぶん照合する
out=$(sh tools/env.sh qemu tmp/s15/fpsoft.bin < /dev/null 2> /dev/null)
[ "$out" = ok ]
report $? "fp: 加減乗除がホストの double とビット一致する"
[ "$out" = ok ] || echo "$out" | head -5

section "第 4 部: libc15 と kernel15 (docs/stage015-tcc.md 11 章)"

# lib15 を libc15 一式 + 実行時支援とリンクし，kernel15 (lseek 持ち) の
# 上で走らせて期待出力と突き合わせる
sh tools/bundle.sh stage015/libc/include/*.h tests/stage015/user/lib15.c \
    2> /dev/null \
    | sh tools/env.sh qemu "$pp" > tmp/s15/lib15.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/lib15.i > tmp/s15/lib15.o 2> /dev/null \
    && { printf 'E'; cat tmp/s15/lib15.o \
         tmp/build/l15_src_string.o tmp/build/l15_src_stdlib.o \
         tmp/build/l15_src_misc15.o tmp/build/l15_posix_sys.o \
         tmp/build/l15_posix_morecore.o tmp/build/l15_posix_stdio.o \
         tmp/build/l15_posix_assert.o tmp/build/rt64.o tmp/build/rtfp.o; \
         printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld14.bin > tmp/s15/lib15
report $? "build: lib15 (libc15 一式 + rt64 + rtfp をリンク)"

rm -rf tmp/s15/root
mkdir -p tmp/s15/root
cp tmp/s15/lib15 tmp/s15/root/lib15
printf 'lib15\n' > tmp/s15/root/boot
sh tools/sfs.sh pack tmp/s15/root tmp/s15/fs.img 4194304 128 > /dev/null
packrc=$?

# **落ちたときに「出力が違う」のか「実行が再現していない」のかを
# 分ける** (docs/dev-notes.md 1.6)。この 2 件は CI で実際に揺らいだ。
# 像は毎回作り直す —— 走らせた側が共有領域を書き換えるので，
# 使い回すと 2 度目の入力が 1 度目と違ってしまう
runlib15() {
    _rk=$1; _ro=$2
    rm -f "tmp/s15/ram-$_rk"
    dd if=/dev/null of="tmp/s15/ram-$_rk" bs=1 seek=134217728 2> /dev/null \
        && dd if=tmp/s15/fs.img of="tmp/s15/ram-$_rk" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_RAMFILE="tmp/s15/ram-$_rk" sh tools/env.sh qemu \
            "tmp/build/$_rk.bin" < /dev/null > "$_ro" 2>&1
}
genk15() { [ "$packrc" -eq 0 ] && runlib15 kernel15 "$1"; }
stable_cmp "lib15(kernel15)" genk15 tests/stage015/expected/lib15.txt
report $? "run: printf %llu / snprintf / strto / sscanf / setjmp / lseek が kernel15 で通る"

# kernel16 (PT_LOAD を全部載せる。第 6 部) が従来の 'E' 形式をこれまで
# どおり読めることを見る。像は上と同じものを使う
genk16() { [ "$packrc" -eq 0 ] && runlib15 kernel16 "$1"; }
stable_cmp "lib15(kernel16)" genk16 tests/stage015/expected/lib15.txt
report $? "run: 同じ像が kernel16 でも同じ出力になる ('E' 形式の後方互換)"

# U モードの浮動小数点 (ld16 の 'K' 前置部が mstatus.FS を立てる)。
# tcc が作った実行形式はハードウェアの浮動小数点を使うので，これが
# 立っていないと不正命令で落ちる (docs/stage015-tcc.md 12.24)
if [ -x tmp/tcc/build/riscv32-tcc ] && [ -d tmp/tcc/build/os ]; then
    rm -rf tmp/s15/fproot
    mkdir -p tmp/s15/fproot
    cat > tmp/s15/fptest.c <<'FPEOF'
#include <stdio.h>
int main(void) { double d = 1.5; printf("fp=%d\n", (int)(d * 4.0)); return 0; }
FPEOF
    cp tmp/s15/fptest.c tmp/tcc/build/os/fptest.c
    ( cd tmp/tcc/build/os \
      && ../riscv32-tcc -nostdinc -I. -c fptest.c -o fptest.o \
      && ../riscv32-tcc -nostdlib -static -Wl,-Ttext=0x86000000 \
           -o ../fptest rt.o fptest.o libc1.o libc2.o ) > /dev/null 2>&1 \
      && cp tmp/tcc/build/fptest tmp/s15/fproot/fptest \
      && printf 'fptest\n' > tmp/s15/fproot/boot \
      && sh tools/sfs.sh pack tmp/s15/fproot tmp/s15/fp.img 1048576 32 > /dev/null \
      && rm -f tmp/s15/fpram \
      && dd if=/dev/null of=tmp/s15/fpram bs=1 seek=134217728 2> /dev/null \
      && dd if=tmp/s15/fp.img of=tmp/s15/fpram bs=64K oflag=seek_bytes \
          seek=67108864 conv=notrunc 2> /dev/null
    out=$(STONE_QEMU_RAMFILE=tmp/s15/fpram sh tools/env.sh qemu \
        tmp/build/kernel16.bin < /dev/null 2>&1)
    [ "$out" = "fp=6" ]
    report $? "run: tcc が出したハードウェア浮動小数点が U モードで走る"
    [ "$out" = "fp=6" ] || echo "   got: $out"
else
    echo "   skip: tmp/tcc/build/os が無い (sh tools/tcc.sh os で作れる)"
fi

section "第 6 部: tcc の自己ホスト (docs/stage015-tcc.md 12 章)"

# cc15m で入れた言語の穴を 1 つずつ確かめる (33 検査を 1 行で出す)
sh tools/bundle.sh tests/stage015/probe/gap15m.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/s15/gap15m.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/gap15m.i > tmp/s15/gap15m.o 2> /dev/null \
    && { cat tmp/s15/gap15m.o tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/gap15m.bin 2> /dev/null
report $? "build: gap15m (cc15m の言語機能の検査) がビルドできる"

out=$(sh tools/env.sh qemu tmp/s15/gap15m.bin < /dev/null 2> /dev/null)
[ "$out" = "abcdefghijklmnopqrstuvwxyzABCDEFG" ]
report $? "run: cc15m の言語機能 33 件がすべて正しい"
[ "$out" = "abcdefghijklmnopqrstuvwxyzABCDEFG" ] || echo "   got: $out"

# 局所の構造体を式で初期化する (cc15o。docs/stage015-tcc.md 12.22)
sh tools/bundle.sh tests/stage015/probe/strinit.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/s15/strinit.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/strinit.i > tmp/s15/strinit.o 2> /dev/null \
    && { cat tmp/s15/strinit.o tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/strinit.bin 2> /dev/null
report $? "build: strinit (局所の構造体の初期化の検査) がビルドできる"

out=$(sh tools/env.sh qemu tmp/s15/strinit.bin < /dev/null 2> /dev/null)
[ "$out" = "abcdef" ]
report $? "run: 局所の構造体を式で初期化する 6 形がすべて正しい"
[ "$out" = "abcdef" ] || echo "   got: $out"

# 構造体の配置 (cc15p。14 章)。期待値の正解は riscv32-tcc に静的表明で
# 確かめてある (probe/layout-oracle.c)。tcc の木があるときはそれも回す
sh tools/bundle.sh tests/stage015/probe/layout.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/s15/layout.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/layout.i > tmp/s15/layout.o 2> /dev/null \
    && { cat tmp/s15/layout.o tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/layout.bin 2> /dev/null
report $? "build: layout (構造体の配置の検査) がビルドできる"

out=$(sh tools/env.sh qemu tmp/s15/layout.bin < /dev/null 2> /dev/null)
[ "$out" = "ok" ]
report $? "run: 構造体の配置 16 項目が riscv32-tcc と一致する"
[ "$out" = "ok" ] || echo "$out" | sed 's/^/   /'

if [ -x tmp/tcc/build/riscv32-tcc ]; then
    tmp/tcc/build/riscv32-tcc -c tests/stage015/probe/layout-oracle.c \
        -o /dev/null 2> /dev/null
    report $? "oracle: layout の期待値が riscv32-tcc の配置と一致する"
fi

# strtod (tcc が 10 進の浮動小数点定数の変換に使う。12.25)
sh tools/bundle.sh stage015/libc/include/*.h \
    "sys/time.h=stage015/libc/include/sys/time.h" \
    tests/stage015/probe/strtod.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/s15/strtod.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/strtod.i > tmp/s15/strtod.o 2> /dev/null \
    && { printf 'E'; cat tmp/s15/strtod.o \
         tmp/build/l15_src_string.o tmp/build/l15_src_ctype.o \
         tmp/build/l15_src_stdlib.o tmp/build/l15_src_misc15.o \
         tmp/build/l15_posix_sys.o tmp/build/l15_posix_morecore.o \
         tmp/build/l15_posix_stdio.o tmp/build/l15_posix_assert.o \
         tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/s15/strtod 2> /dev/null
report $? "build: strtod の検査がビルドできる"

rm -rf tmp/s15/sdroot
mkdir -p tmp/s15/sdroot
cp tmp/s15/strtod tmp/s15/sdroot/sd
printf 'sd\n' > tmp/s15/sdroot/boot
sh tools/sfs.sh pack tmp/s15/sdroot tmp/s15/sd.img 1048576 32 > /dev/null \
    && rm -f tmp/s15/sdram \
    && dd if=/dev/null of=tmp/s15/sdram bs=1 seek=134217728 2> /dev/null \
    && dd if=tmp/s15/sd.img of=tmp/s15/sdram bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
out=$(STONE_QEMU_RAMFILE=tmp/s15/sdram sh tools/env.sh qemu \
    tmp/build/kernel16.bin < /dev/null 2>&1)
[ "$out" = "abcdefg" ]
report $? "run: strtod が 2 の冪 (2^32 / 2^64 / 2^96) を正しく変換する"
[ "$out" = "abcdefg" ] || echo "   got: $out"

# 整数定数の型 (cc15n)。0x80000000 は unsigned int である
sh tools/bundle.sh tests/stage015/probe/litu.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/s15/litu.i 2> /dev/null \
    && sh tools/env.sh qemu "$cc" < tmp/s15/litu.i > tmp/s15/litu.o 2> /dev/null \
    && { cat tmp/s15/litu.o tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > tmp/s15/litu.bin 2> /dev/null
report $? "build: litu (整数定数の型の検査) がビルドできる"

out=$(sh tools/env.sh qemu tmp/s15/litu.bin < /dev/null 2> /dev/null)
[ "$out" = "abcdefg" ]
report $? "run: 整数定数の型が C89 のとおり (0x80000000 は unsigned int)"
[ "$out" = "abcdefg" ] || echo "   got: $out"

# pp16 の再帰抑止 (自己参照マクロを実引数に渡す)
sh tools/bundle.sh tests/stage015/probe/hyg16.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp16.bin 2> /dev/null \
    | tr -d '\004' | grep -v '^ *$' > tmp/s15/hyg16.out
diff -q tmp/s15/hyg16.out tests/stage015/expected/hyg16.txt > /dev/null 2>&1
report $? "pp16: 自己参照マクロの実引数が gcc と同じ展開になる"

# pp16 は既存のソースの前処理結果を変えない
ok=0
for f in src/misc15 posix/stdio posix/sys; do
    n=$(echo "$f" | tr / _)
    sh tools/bundle.sh stage015/libc/include/*.h \
        "sys/time.h=stage015/libc/include/sys/time.h" "stage015/libc/$f.c" \
        > "tmp/s15/h_$n.bundle" 2> /dev/null
    sh tools/env.sh qemu tmp/build/pp15.bin < "tmp/s15/h_$n.bundle" \
        > "tmp/s15/h_$n.old" 2> /dev/null
    sh tools/env.sh qemu tmp/build/pp16.bin < "tmp/s15/h_$n.bundle" \
        > "tmp/s15/h_$n.new" 2> /dev/null
    cmp -s "tmp/s15/h_$n.old" "tmp/s15/h_$n.new" || ok=1
done
[ "$ok" -eq 0 ]
report $? "pp16: 既存のソースの前処理結果が pp15 と変わらない"

section "第 5 部: tcc に RV32 の対象を足す (docs/stage015-riscv32.md)"

# 素材とホストの道具があるときだけ走る。ここで作るのは**ホストの gcc が
# 作った tcc** であり，ブートストラップ鎖の一部ではない (tools/tcc.sh の
# 頭の注意書き)。素材の無い環境 (CI) では飛ばす
tccdir=docs/external/tcc
if [ ! -d "$tccdir" ]; then
    echo "   skip: $tccdir が無い (sh tools/fetch.sh tcc で取得できる)"
elif ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "   skip: ホストに gcc / make が無い"
elif ! command -v patch >/dev/null 2>&1; then
    echo "   skip: ホストに patch が無い"
else
    # RV64 を壊していないこと。riscv32.patch は RV32 と RV64 で共通の
    # ファイル (riscv64-gen.c) を触るので，ここは毎回見る必要がある
    sh tools/tcc.sh riscv64 > tmp/s15/tcc-riscv64.log 2>&1
    report $? "tcc: 上流の RV64 対象が patch 適用後もビルドできる"

    sh tools/tcc.sh riscv32 > tmp/s15/tcc-riscv32.log 2>&1
    report $? "tcc: RV32 対象の交差コンパイラ (riscv32-tcc) がビルドできる"

    # patch は RV32 と RV64 で同じファイル (riscv64-gen.c) を触る。
    # 「ビルドが通る」だけでは足りないので，**素の tcc と同じバイト列の
    # オブジェクトを吐くこと**まで見る。XLEN が 8 のときは以前と同じ定数に
    # 畳まれるはずで，畳まれていなければここで捕まる
    sh tools/tcc.sh base > tmp/s15/tcc-base.log 2>&1
    report $? "tcc: patch を当てない素の RV64 tcc がビルドできる (対照)"

    # 対象は tcc 自身の実行時支援 (自前のヘッダだけで閉じており，RV64 の
    # sysroot が無くてもコンパイルできる) と，その 2 の検査ソース。
    # libtcc1.c は 64 bit 演算と浮動小数点の塊なので効きがよい
    same=0
    # **入力は両方とも素の木から取る。** オブジェクトにはソースの経路が
    # 入るので，別の木を食わせると経路の差だけで違うバイト列になる
    for f in lib/libtcc1.c lib/builtin.c lib/stdatomic.c lib/va_list.c \
             lib/armflush.c lib/dsohandle.c; do
        rm -f tmp/s15/tcc-a.o tmp/s15/tcc-b.o
        src=tmp/tcc/base/src/$f
        tmp/tcc/base/build/riscv64-tcc -c "$src" \
            -o tmp/s15/tcc-a.o -B tmp/tcc/base/src -I tmp/tcc/base/build \
            > /dev/null 2>&1 || { same=1; echo "   base が $f を通せない"; }
        tmp/tcc/build/riscv64-tcc -c "$src" \
            -o tmp/s15/tcc-b.o -B tmp/tcc/base/src -I tmp/tcc/base/build \
            > /dev/null 2>&1 || { same=1; echo "   patch 後が $f を通せない"; }
        cmp -s tmp/s15/tcc-a.o tmp/s15/tcc-b.o || { same=1; echo "   diff: $f"; }
    done
    rm -f tmp/s15/tcc-a.o tmp/s15/tcc-b.o
    tmp/tcc/base/build/riscv64-tcc -c tests/stage015/tccprobe/int32.c \
        -o tmp/s15/tcc-a.o -B tmp/tcc/base/src > /dev/null 2>&1 || same=1
    tmp/tcc/build/riscv64-tcc -c tests/stage015/tccprobe/int32.c \
        -o tmp/s15/tcc-b.o -B tmp/tcc/base/src > /dev/null 2>&1 || same=1
    cmp -s tmp/s15/tcc-a.o tmp/s15/tcc-b.o || { same=1; echo "   diff: tccprobe/int32.c"; }
    [ "$same" -eq 0 ]
    report $? "tcc: patch 適用後も RV64 の生成コードがバイト単位で変わらない"

    if [ -x tmp/tcc/build/riscv32-tcc ]; then
        printf 'int add(int a, int b) { return a + b; }\n' > tmp/s15/tccadd.c
        tmp/tcc/build/riscv32-tcc -c tmp/s15/tccadd.c -o tmp/s15/tccadd.o \
            > /dev/null 2>&1
        report $? "tcc: riscv32-tcc が C をオブジェクトへ通す"

        # ELF の見出しだけを見る。ELFCLASS32 (5 バイト目 = 1) と
        # e_machine = EM_RISCV (243) であること
        cls=$(od -An -tu1 -j 4 -N 1 tmp/s15/tccadd.o | tr -d ' ')
        mach=$(od -An -tu2 -j 18 -N 2 tmp/s15/tccadd.o | tr -d ' ')
        [ "$cls" = 1 ] && [ "$mach" = 243 ]
        report $? "tcc: 吐いたオブジェクトが ELF32 の RISC-V である (class=$cls machine=$mach)"

        # 整数の範囲を一通り吐かせ，**RV32 に無い命令が 1 つも無い**ことを
        # 見る。逆アセンブラは不正な語を .insn と表示するので数えられる
        # (docs/stage015-riscv32.md 7 章)
        if ! command -v riscv64-unknown-elf-objdump > /dev/null 2>&1; then
            echo "   skip: riscv64-unknown-elf-objdump が無い"
        else
            tmp/tcc/build/riscv32-tcc -c tests/stage015/tccprobe/int32.c \
                -o tmp/s15/tccint32.o > /dev/null 2>&1
            report $? "tcc: 整数の一通り (tccprobe/int32.c) がオブジェクトになる"

            bad=$(riscv64-unknown-elf-objdump -d tmp/s15/tccint32.o \
                  | grep -c '\.insn' || true)
            [ "$bad" -eq 0 ]
            report $? "tcc: 生成コードに RV32 に無い命令が無い (不正な語 $bad 個)"
        fi

        # 実行時支援 (riscv32-libtcc1.a) が通ること。可変桁の 64 bit
        # シフトなどはここを呼ぶ (docs/stage015-riscv32.md 12 章)
        make -C tmp/tcc/build cross-riscv32 > tmp/s15/tcc-lib.log 2>&1
        report $? "tcc: RV32 の実行時支援 (riscv32-libtcc1.a) がビルドできる"

        # **走らせて答を見る。** 不正命令が無いことは「命令が RV32 の
        # ものである」ことしか言わない。正しい命令が選ばれていることは
        # 実行しないと判らない (docs/stage015-riscv32.md 7 章)
        tmp/tcc/build/riscv32-tcc -static -nostdlib -Wl,-Ttext=0x80000000 \
            -o tmp/s15/run32.elf \
            tests/stage015/tccprobe/head.S tests/stage015/tccprobe/run32.c \
            > /dev/null 2>&1
        report $? "tcc: 実走用の像 (head.S + run32.c) がリンクできる"

        # 乗除算・シフト・幅の狭い型・繰返し・配列。char は符号なし (RISC-V の ABI)
        want='006ae9bc:ffffff72:00000006:00300000:ffffff80:00f00000:0000eaf7:000013ba:00000054'
        got=$(sh tools/env.sh qemu tmp/s15/run32.elf < /dev/null 2>/dev/null)
        [ "$got" = "$want" ]
        report $? "tcc: 吐いたものが我々の QEMU で走り，答が合う"
        [ "$got" = "$want" ] || { echo "     期待 $want"; echo "     実測 $got"; }

        # 呼出し規約 (その 3)。実行時支援を並べて走らせる
        tccrun() {
            nm=$1; want=$2
            tmp/tcc/build/riscv32-tcc -static -nostdlib \
                -I tmp/tcc/src/include \
                -Wl,-Ttext=0x80000000 -o "tmp/s15/$nm.elf" \
                tests/stage015/tccprobe/head.S \
                "tests/stage015/tccprobe/$nm.c" \
                tmp/tcc/build/riscv32-libtcc1.a > /dev/null 2>&1 || {
                report 1 "tcc: $nm がリンクできない"
                return
            }
            got=$(sh tools/env.sh qemu "tmp/s15/$nm.elf" < /dev/null 2>/dev/null)
            [ "$got" = "$want" ]
            report $? "tcc: $nm が我々の QEMU で走り，答が合う"
            [ "$got" = "$want" ] || { echo "     期待 $want"; echo "     実測 $got"; }
        }
        tccrun ll32 '000462d5080063b1:0000000100000002:00000000ffffffff:0000002471c71c5c:00000001:00000181:0000000200000006'
        tccrun llcmp32 '00000001:00000000:00000001:00000001:00000001:00000001:00000001:00000001'
        tccrun struct32 '000002c5:00000032:00000041:000010e1'
        tccrun float32 '40700000:3fc00000:400e000000000000:3ff8000000000000:3fd0000000000000:00000001:c01c000000000000:fffffff9:4271f71fb04cb000:d4a51000:3f000000:3fe0000000000000'
        tccrun vfp32 '4008000000000000:3ff0000000000000:4072e80000000000:3ffc000000000000'

        # 実物の C (その 5)。bzip2 1.0.8 の libbz2 を無改変で通す。
        # 素材があるときだけ走る
        bzdir=docs/external/bzip2
        if [ ! -d "$bzdir" ]; then
            echo "   skip: $bzdir が無い (sh tools/fetch.sh bzip2 で取得できる)"
        else
            mkdir -p tmp/s15/bz
            bzok=0
            for f in blocksort huffman crctable randtable compress decompress bzlib; do
                rm -f "tmp/s15/bz/$f.o"
                tmp/tcc/build/riscv32-tcc -c -DBZ_NO_STDIO=1 -nostdinc \
                    -I stage014/libc/include -I "$bzdir" -B tmp/tcc/src \
                    -o "tmp/s15/bz/$f.o" "$bzdir/$f.c" > /dev/null 2>&1 || bzok=1
            done
            [ "$bzok" -eq 0 ]
            report $? "tcc: libbz2 (1.0.8) の 7 ファイルを riscv32-tcc が無改変で通す"

            tmp/tcc/build/riscv32-tcc -static -nostdlib -nostdinc \
                -I stage014/libc/include -I "$bzdir" -B tmp/tcc/src \
                -Wl,-Ttext=0x80000000 -o tmp/s15/bz32.elf \
                tests/stage015/tccprobe/head.S tests/stage015/tccprobe/bz32.c \
                tmp/s15/bz/*.o tmp/tcc/build/riscv32-libtcc1.a > /dev/null 2>&1
            report $? "tcc: libbz2 + 検査ドライバがリンクできる"

            # 圧縮の返り値・長さ・検査和，伸長の返り値・長さ，一致した文字数。
            # 長さ 0x14b と検査和 86cd7fff は**ホストの bzip2 と同じ値**である
            # (docs/stage015-riscv32.md 14 章)
            want='00000000:0000014b:86cd7fff:00000000:00001000:00001000'
            got=$(sh tools/env.sh qemu tmp/s15/bz32.elf < /dev/null 2>/dev/null)
            [ "$got" = "$want" ]
            report $? "tcc: bzip2 が我々の QEMU で圧縮・伸長し，ホストと同じバイト列を出す"
            [ "$got" = "$want" ] || { echo "     期待 $want"; echo "     実測 $got"; }
        fi
    else
        report 1 "tcc: riscv32-tcc が無いので出力を見られない"
    fi
fi

summary
