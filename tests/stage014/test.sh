#!/bin/bash
# Stage 14 テスト: 適合台帳の照合 (docs/stage014-external.md 4 章)。
#
# probe/ の各ソースを pp -> cc -> ld に通し，結果を ledger.txt と突き合わせる。
#
#   ok   通ってその出力になる
#   gap  cc がその終了コードで拒む (未対応。拒むこと自体は正しい振舞い)
#   bad  通ってしまうが結果が誤っている
#
# **台帳と実測が食い違えば失敗する。** 直したのに表がそのままでも，
# 壊したのに気づかなくても，等しく捕まえるためである。gap が ok に
# 変わったらそれは前進なので，表を直して commit する。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s14
stable_dir=tmp/s14/stable

cc=tmp/build/cc14g.bin    # 台帳は最前線の世代で測る (docs/stage014-external.md 5.3)
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage014/probe
hdr=stage013/libc/include/stdarg.h

ensure_build stage014

# probe を 1 つ通す。結果を "状態 値" の形で標準出力へ返す
#   gap <cc の終了コード> / ok <出力> / linkfail / runfail <終了コード>
probe() {
    n=$1
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/$n.i" 2> /dev/null || {
        echo "ppfail"
        return
    }
    sh tools/env.sh qemu "$cc" < "tmp/s14/$n.i" > "tmp/s14/$n.o" 2> /dev/null
    ccrc=$?
    if [ "$ccrc" -ne 0 ]; then
        echo "gap $ccrc"
        return
    fi
    { cat "tmp/s14/$n.o"; printf '\0'; } \
        | sh tools/env.sh qemu "$ld" > "tmp/s14/$n.bin" 2> /dev/null || {
        echo "linkfail"
        return
    }
    out=$(sh tools/env.sh qemu "tmp/s14/$n.bin" < /dev/null 2> /dev/null)
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "runfail $rc"
        return
    fi
    # 改行を \n として 1 行に畳む (台帳に書ける形にする)
    printf 'ok %s\n' "$(printf '%s\n' "$out" | sed -e 's/\\/\\\\/g' -e 's/\t/\\t/g' | tr '\n' '@' | sed 's/@/\\n/g')"
}

section "ビルド再現と固定点"

ok=0
for pair in cc14a.bin:stage014/cc14.md cc14b.bin:stage014/cc14b.md \
        cc14c.bin:stage014/cc14c.md cc14d.bin:stage014/cc14d.md \
        cc14e.bin:stage014/cc14e.md cc14f.bin:stage014/cc14f.md \
        cc14g.bin:stage014/cc14g.md \
        ld14.bin:stage014/ld14.md pp14.bin:stage014/pp14.md; do
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "${pair##*:}" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/${pair%%:*}"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: cc14a..cc14g と ld14 / pp14 の SHA-256 が各 .md 記載値と一致"

# 世代を触ったら必ず固定点を見る (docs/dev-notes.md 3.1)。
#
# **落ちたときに「中身が違う」のか「実行が再現していない」のかを
# 分ける** (1.6)。この検査は CI で実際に揺らいだ —— 成果物の SHA-256
# 照合は通るのに固定点だけが落ちる，という 1.6 の型そのものだった
fp14gen() {
    { cat stage014/cc14g.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc14g.bin > tmp/s14/b3.o \
        && { cat tmp/s14/b3.o; printf '\0'; } \
            | sh tools/env.sh qemu "$ld" > "$1"
}
stable_cmp "fixpoint(cc14g)" fp14gen tmp/build/cc14g.bin
report $? "fixpoint: cc14g が自分自身を再生成する (B2 == B3)"

# コード生成を触ったのは 2048 バイト以上のフレームの経路だけで，
# 既存のソースには影響しない。cc10l と同じオブジェクトになることで示す
ok=0
for n in sh ed mk; do
    sh tools/bundle.sh stage013/libc/include/*.h "stage013/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/r_$n.i" 2> /dev/null \
        && sh tools/env.sh qemu "$cc" < "tmp/s14/r_$n.i" > "tmp/s14/r_$n.o" 2> /dev/null \
        && cmp -s "tmp/s14/r_$n.o" "tmp/build/${n}13.o" || ok=1
done
[ "$ok" -eq 0 ]
report $? "regress: cc14g が既存のソース (sh / ed / mk) を cc10l と同じ .o にする"

# pp14 (第 9 部): 容量拡大の実測。長い名前と大きな入力が通り，
# 短い入力に対しては pp と同じ出力になる
printf '#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR\nint x;\n#endif\nint ok;\n\004' \
    | sh tools/env.sh qemu tmp/build/pp14.bin > tmp/s14/pp14a.out 2> /dev/null \
    && grep -q "int ok" tmp/s14/pp14a.out
report $? "pp14: 40 文字のマクロ名が通る (pp は容量超過 6 で拒む)"

sh tools/bundle.sh stage013/libc/include/*.h stage013/sh.c 2> /dev/null \
    | sh tools/env.sh qemu "$pp" > tmp/s14/pp14b.pp 2> /dev/null
sh tools/bundle.sh stage013/libc/include/*.h stage013/sh.c 2> /dev/null \
    | sh tools/env.sh qemu tmp/build/pp14.bin > tmp/s14/pp14b.pp14 2> /dev/null
cmp -s tmp/s14/pp14b.pp tmp/s14/pp14b.pp14
report $? "pp14: 既存ソース (sh.c) の前処理結果が pp と一致する"

section "適合台帳の照合"

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
        # ok と bad は「通って，その出力になる」ことを見る。違いは意味づけだけ
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
done < tests/stage014/ledger.txt

echo
echo "   台帳: 通る $nok 件 / 未対応 $ngap 件 / 通るが誤り $nbad 件"

# 台帳は行駆動なので，probe/ に足しただけで台帳に載せていないファイルは
# 黙って検査されない。1:1 であることを確かめる (probe を足したのに台帳を
# 直し忘れた，を捕まえる)
sed 's/#.*//' tests/stage014/ledger.txt | awk 'NF { print $1 }' | sort > tmp/s14/ledger.names
find "$prb" -name '*.c' -exec basename {} .c \; | sort > tmp/s14/probe.names
diff -u tmp/s14/probe.names tmp/s14/ledger.names > tmp/s14/names.diff
rc=$?
[ "$rc" -eq 0 ] || { echo "   (- が probe/ のみ，+ が台帳のみ)"; sed -n '4,$p' tmp/s14/names.diff; }
report $rc "台帳: probe/*.c と ledger.txt の項目が 1:1"

# ---------------------------------------------------------------------------
section "libc 第 14 世代 (第 7 部)"

RAMSIZE=134217728
SFSOFF=67108864
IMGSIZE=4194304

# OS (kernel13) の上で走らせる。stage013 のテストと同じ道具立て
runos14() {
    sh tools/sfs.sh pack tmp/s14/root tmp/s14/fs.img "$IMGSIZE" 128 || return 1
    rm -f tmp/s14/ram
    dd if=/dev/null of=tmp/s14/ram bs=1 seek="$RAMSIZE" 2> /dev/null
    dd if=tmp/s14/fs.img of=tmp/s14/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
        conv=notrunc 2> /dev/null
    STONE_QEMU_RAMFILE=tmp/s14/ram sh tools/env.sh qemu tmp/build/kernel13.bin \
        < /dev/null > "tmp/s14/$1.out" 2>&1
}

build14() {
    sh tools/bundle.sh stage014/libc/include/*.h "tests/stage014/user/$1.c" \
        | sh tools/env.sh qemu "$pp" > "tmp/s14/$1.i" \
        && sh tools/env.sh qemu "$cc" < "tmp/s14/$1.i" > "tmp/s14/$1.o" \
        && { printf 'E'; cat "tmp/s14/$1.o" tmp/build/l14_src_string.o \
             tmp/build/l14_src_stdlib.o tmp/build/l14_posix_sys.o \
             tmp/build/l14_posix_morecore.o tmp/build/l14_posix_stdio.o \
             tmp/build/l14_posix_assert.o; printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld14.bin > "tmp/s14/$1"
}

build14 lib14
report $? "build: lib14 (printf 拡張・sprintf・assert を使う)"

rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/s14/lib14 tmp/s14/root/lib14
printf 'lib14\n' > tmp/s14/root/boot
runos14 lib14
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s14/lib14.out tests/stage014/expected/lib14.txt > /dev/null
report $? "run: printf の l / 左詰め / %s の幅，sprintf，assert (成立)"

build14 abrt
report $? "build: abrt (assert の失敗)"

rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/s14/abrt tmp/s14/root/abrt
printf 'abrt\n' > tmp/s14/root/boot
runos14 abrt
rc=$?
[ "$rc" -eq 1 ] && diff -q tmp/s14/abrt.out tests/stage014/expected/abrt.txt > /dev/null
report $? "run: assert の失敗が式の文字列を出して exit(1) する"

# ---------------------------------------------------------------------------
section "第 8 部: 外部ソース (bzip2) のビルドと実走"

# 素材があるときだけ走る (docs/stage014-external.md 2.2)。無ければ理由を
# 出して飛ばす。素材の有無で結果が変わる検査を必須にすると環境の差で
# チェーン全体が赤くなるためである
bzdir=docs/external/bzip2
if [ ! -d "$bzdir" ]; then
    echo "   skip: $bzdir が無い (sh tools/fetch.sh bzip2 で取得できる)"
else
    inc=stage014/libc/include

    # libbz2 の 7 ファイルを BZ_NO_STDIO でコンパイルする。ソースは無改変。
    # 設定マクロは repo 側の包み (tmp に生成) が与える
    bzok=0
    for f in blocksort huffman crctable randtable compress decompress bzlib; do
        printf '#define BZ_NO_STDIO 1\n#include "%s.c"\n' "$f" > "tmp/s14/bz_$f.c"
        sh tools/bundle.sh "$inc/stddef.h" "$inc/stdlib.h" \
                "$bzdir/bzlib.h" "$bzdir/bzlib_private.h" "$bzdir/$f.c" \
                "tmp/s14/bz_$f.c" 2> /dev/null \
            | sh tools/env.sh qemu "$pp" 2> /dev/null \
            | sh tools/env.sh qemu "$cc" > "tmp/s14/bz_$f.o" 2> /dev/null || bzok=1
    done
    report $bzok "build: libbz2 (1.0.8) の 7 ファイルが無改変でコンパイルできる"

    # 検査ドライバ (圧縮 -> 伸長 -> 一致) を組み，ld14 でリンクする
    sh tools/bundle.sh "$inc/stddef.h" "$inc/stdarg.h" "$inc/stdio.h" \
            "$inc/stdlib.h" "$bzdir/bzlib.h" tests/stage014/user/bzt.c 2> /dev/null \
        | sh tools/env.sh qemu "$pp" 2> /dev/null \
        | sh tools/env.sh qemu "$cc" > tmp/s14/bzt.o 2> /dev/null \
        && { printf 'E'; cat tmp/s14/bzt.o \
             tmp/s14/bz_blocksort.o tmp/s14/bz_huffman.o tmp/s14/bz_crctable.o \
             tmp/s14/bz_randtable.o tmp/s14/bz_compress.o tmp/s14/bz_decompress.o \
             tmp/s14/bz_bzlib.o \
             tmp/build/l14_src_string.o tmp/build/l14_src_stdlib.o \
             tmp/build/l14_posix_sys.o tmp/build/l14_posix_morecore.o \
             tmp/build/l14_posix_stdio.o tmp/build/l14_posix_assert.o; \
             printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld14.bin > tmp/s14/bzt
    report $? "link: 検査ドライバ + libbz2 + libc14 (ld14。31 バイトの記号名)"

    # kernel13 の上で走らせ，圧縮と往復一致の出力を照合する
    rm -rf tmp/s14/root
    mkdir -p tmp/s14/root
    cp tmp/s14/bzt tmp/s14/root/bzt
    printf 'bzt\n' > tmp/s14/root/boot
    runos14 bzt
    rc=$?
    [ "$rc" -eq 0 ] && diff -q tmp/s14/bzt.out tests/stage014/expected/bzt.txt > /dev/null
    report $? "run: 4096 バイトを圧縮 2555 バイト，伸長して往復一致"

    # 圧縮出力が正規の bzip2 ストリームであること (形式の相互運用)。
    # ホストに bzip2 が無ければ飛ばす
    if command -v bzip2 > /dev/null 2>&1; then
        dd if=tmp/s14/ram of=tmp/s14/fs_after.img bs=64K iflag=skip_bytes \
            skip="$SFSOFF" count=64 2> /dev/null
        rm -rf tmp/s14/after
        sh tools/sfs.sh unpack tmp/s14/fs_after.img tmp/s14/after > /dev/null \
            && bzip2 -d < tmp/s14/after/out.bz2 | cmp -s - tmp/s14/after/src.bin
        report $? "interop: 圧縮出力をホストの bzip2 -d が伸長し原文と一致"
    else
        echo "   skip: ホストに bzip2 が無い (相互運用の検査)"
    fi
fi

# ---------------------------------------------------------------------------
section "第 9 部: 外部ソース (zlib) のビルドと実走"

# 素材があるときだけ走る (docs/stage014-external.md 2.2)
zdir=docs/external/zlib
if [ ! -d "$zdir" ]; then
    echo "   skip: $zdir が無い (sh tools/fetch.sh zlib で取得できる)"
else
    inc=stage014/libc/include

    # zlib のコアを Z_SOLO (stdio・OS 非依存の構成) でコンパイルする。
    # ソースは無改変で，設定マクロは repo 側の包み (tmp に生成) が与える。
    # 前処理は pp14 (pp では容量が足りない。13.1)
    zok=0
    for f in adler32 crc32 zutil inftrees inffast inflate infback trees \
             deflate compress uncompr; do
        case "$f" in
        crc32)    zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/crc32.h" ;;
        inftrees) zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/inftrees.h" ;;
        inffast)  zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/inftrees.h \
                        $zdir/inflate.h $zdir/inffast.h" ;;
        inflate|infback)
                  zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/inftrees.h \
                        $zdir/inflate.h $zdir/inffast.h $zdir/inffixed.h" ;;
        trees)    zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/deflate.h \
                        $zdir/trees.h" ;;
        deflate)  zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h $zdir/deflate.h" ;;
        compress|uncompr)
                  zdep="$zdir/zconf.h $zdir/zlib.h" ;;
        *)        zdep="$zdir/zconf.h $zdir/zlib.h $zdir/zutil.h" ;;
        esac
        printf '#define Z_SOLO 1\n#include "%s.c"\n' "$f" > "tmp/s14/z_$f.c"
        sh tools/bundle.sh $zdep "$zdir/$f.c" "tmp/s14/z_$f.c" 2> /dev/null \
            | sh tools/env.sh qemu tmp/build/pp14.bin 2> /dev/null \
            | sh tools/env.sh qemu "$cc" > "tmp/s14/z_$f.o" 2> /dev/null || zok=1
    done
    report $zok "build: zlib (1.3.1) のコア 11 ファイルが無改変でコンパイルできる"

    # 検査ドライバ (crc32/adler32 と deflate -> inflate の往復) を組む
    sh tools/bundle.sh "$inc/stddef.h" "$inc/stdarg.h" "$inc/stdio.h" \
            "$inc/stdlib.h" "$zdir/zconf.h" "$zdir/zlib.h" \
            tests/stage014/user/zt.c 2> /dev/null \
        | sh tools/env.sh qemu tmp/build/pp14.bin 2> /dev/null \
        | sh tools/env.sh qemu "$cc" > tmp/s14/zt.o 2> /dev/null \
        && { printf 'E'; cat tmp/s14/zt.o \
             tmp/s14/z_adler32.o tmp/s14/z_crc32.o tmp/s14/z_zutil.o \
             tmp/s14/z_inftrees.o tmp/s14/z_inffast.o tmp/s14/z_inflate.o \
             tmp/s14/z_infback.o tmp/s14/z_trees.o tmp/s14/z_deflate.o \
             tmp/build/l14_src_string.o tmp/build/l14_src_stdlib.o \
             tmp/build/l14_posix_sys.o tmp/build/l14_posix_morecore.o \
             tmp/build/l14_posix_stdio.o tmp/build/l14_posix_assert.o; \
             printf '\0'; } \
            | sh tools/env.sh qemu tmp/build/ld14.bin > tmp/s14/zt
    report $? "link: 検査ドライバ + zlib + libc14 (ld14)"

    rm -rf tmp/s14/root
    mkdir -p tmp/s14/root
    cp tmp/s14/zt tmp/s14/root/zt
    printf 'zt\n' > tmp/s14/root/boot
    runos14 zt
    rc=$?
    [ "$rc" -eq 0 ] && diff -q tmp/s14/zt.out tests/stage014/expected/zt.txt > /dev/null
    report $? "run: crc32 / adler32 と deflate 4096 -> 2154，伸長して往復一致"

    # 圧縮出力が正規の zlib ストリームであること (形式の相互運用)。
    # ホストに python3 の zlib が無ければ飛ばす
    if python3 -c 'import zlib' > /dev/null 2>&1; then
        dd if=tmp/s14/ram of=tmp/s14/fs_after.img bs=64K iflag=skip_bytes \
            skip="$SFSOFF" count=64 2> /dev/null
        rm -rf tmp/s14/after
        sh tools/sfs.sh unpack tmp/s14/fs_after.img tmp/s14/after > /dev/null \
            && python3 -c 'import sys, zlib
src = open("tmp/s14/after/src.bin", "rb").read()
zz = open("tmp/s14/after/out.zz", "rb").read()
sys.exit(0 if zlib.decompress(zz) == src else 1)'
        report $? "interop: 圧縮出力をホストの zlib が伸長し原文と一致"
    else
        echo "   skip: ホストに python3 の zlib が無い (相互運用の検査)"
    fi
fi

section "第 10 部: カーネル第 14 世代 (kernel14)"

want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' stage014/kernel14.md | cut -d' ' -f2)
got=$(sha256sum tmp/build/kernel14.bin); got=${got%% *}
[ -n "$want" ] && [ "$want" = "$got" ]
report $? "build: kernel14.bin の SHA-256 が kernel14.md 記載値と一致"

# 検査用のフィルタ (生のスタブだけ。libc を並べない)
{ cat tests/stage014/user/cpfilt.c; printf '\004'; } \
    | sh tools/env.sh qemu "$cc" > tmp/s14/cpfilt.o 2> /dev/null \
    && { printf 'E'; cat tmp/s14/cpfilt.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld14.bin > tmp/s14/cpfilt
report $? "build: cpfilt (標準入出力を写すフィルタ)"

# root/ を仕立てて指定のカーネルで走らせ，イメージを回収して展開する。
# $1 = カーネル, $2 = 出力名, $3 = シェルへ流し込む行
runk() {
    sh tools/sfs.sh pack tmp/s14/root tmp/s14/fs.img "$IMGSIZE" 128 || return 1
    rm -f tmp/s14/ram
    dd if=/dev/null of=tmp/s14/ram bs=1 seek="$RAMSIZE" 2> /dev/null
    dd if=tmp/s14/fs.img of=tmp/s14/ram bs=64K oflag=seek_bytes seek="$SFSOFF" \
        conv=notrunc 2> /dev/null
    # 末尾に EOT を置く。シェルは `exit` でも終わるが，取りこぼしたときに
    # UART の read が永久に待つ (kernel の fd 0 はブロックする) ため，
    # 入力の終わりを必ず伝える
    printf '%s\004' "$3" | STONE_QEMU_RAMFILE=tmp/s14/ram \
        sh tools/env.sh qemu "$1" > "tmp/s14/$2.out" 2>&1
    krc=$?
    dd if=tmp/s14/ram of=tmp/s14/fs2.img bs=64K iflag=skip_bytes,count_bytes \
        skip="$SFSOFF" count="$IMGSIZE" 2> /dev/null
    rm -rf "tmp/s14/out.$2"
    sh tools/sfs.sh unpack tmp/s14/fs2.img "tmp/s14/out.$2" > /dev/null 2>&1
    return $krc
}

# --- #44: 既存ファイルの上書きが隣接を壊さず，カーソルも巻き戻らない ---
#
# pack は名前順に詰めるので割付け順は a.txt, b.txt, boot, cpfilt, in.txt, sh。
# a.txt (3 バイト = 割付け 4) を in.txt の中身で書き直すと元の割付けを
# はみ出す。kernel13 は直後の b.txt を潰し，さらにカーソルが巻き戻って
# 次の新規作成 (new.txt) が in.txt 自身に重なった
rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/build/sh13 tmp/s14/root/sh
cp tmp/s14/cpfilt tmp/s14/root/cpfilt
printf 'sh\n' > tmp/s14/root/boot
printf 'abc' > tmp/s14/root/a.txt
printf 'KEEP-THIS-FILE-INTACT-0123456789' > tmp/s14/root/b.txt
printf 'this is a much longer input line than four bytes\n' > tmp/s14/root/in.txt
runk tmp/build/kernel14.bin ovw \
    'cpfilt < in.txt > a.txt
cpfilt < in.txt > new.txt
exit
'
report $? "run: 上書きと新規作成を含むシェル行が kernel14 で通る"

o=tmp/s14/out.ovw
cmp -s "$o/a.txt" tmp/s14/root/in.txt
report $? "sfs: 割付けを越えた上書きの内容が正しい (a.txt)"

cmp -s "$o/b.txt" tmp/s14/root/b.txt
report $? "sfs: 直後に割り付けられたファイルが無傷 (b.txt)"

cmp -s "$o/in.txt" tmp/s14/root/in.txt
report $? "sfs: 上書き後の新規作成が既存に重ならない (in.txt が無傷)"

cmp -s "$o/new.txt" tmp/s14/root/in.txt
report $? "sfs: 上書き後に作った新規ファイルの内容が正しい (new.txt)"

# --- #49: 載せ先がユーザ領域の外にある ELF を拒む ---
#
# 正しい実行形式の p_vaddr をカーネル本体 (0x8000_0000) へ向ける。
# kernel13 はこれをそのまま複写して自分を壊し，機械ごと止まった
r32() { od -An -tu4 -j "$2" -N4 "$1" | tr -d ' '; }
w32() {
    _v=$3
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' \
        $((_v & 255)) $((_v >> 8 & 255)) $((_v >> 16 & 255)) $((_v >> 24 & 255)))" \
        | dd of="$1" bs=1 seek="$2" conv=notrunc 2> /dev/null
}
cp tmp/s14/cpfilt tmp/s14/badelf
ph=$(r32 tmp/s14/badelf 28)
w32 tmp/s14/badelf $((ph + 8)) $((0x80000000))

rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/build/sh13 tmp/s14/root/sh
cp tmp/s14/badelf tmp/s14/root/badelf
printf 'sh\n' > tmp/s14/root/boot
runk tmp/build/kernel14.bin badelf 'badelf
exit
'
report $? "run: 壊れた ELF を起動してもカーネルが生き残りシェルが続く"

grep -q 'errno 8' tmp/s14/badelf.out
report $? "elf: 載せ先がユーザ領域の外なら ENOEXEC (8) で拒む"

# --- 退行: kernel13 で動いていたものが kernel14 でも同じ結果になる ---
rm -rf tmp/s14/root
mkdir -p tmp/s14/root
cp tmp/s14/lib14 tmp/s14/root/lib14
printf 'lib14\n' > tmp/s14/root/boot
runk tmp/build/kernel14.bin lib14k14 ''
rc=$?
[ "$rc" -eq 0 ] && diff -q tmp/s14/lib14k14.out tests/stage014/expected/lib14.txt > /dev/null
report $? "regress: lib14 が kernel14 でも kernel13 と同じ出力になる"

summary
