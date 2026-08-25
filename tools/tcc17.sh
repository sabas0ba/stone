#!/bin/sh
# tcc の翻訳単位を**我々の OS の上で**1 本ずつ訳す (第 3 部の 3 の 2)。
#
#   tcc17.sh root          作業用の像の元 (tmp/s17/root) を組む
#   tcc17.sh unit <名前>   翻訳単位を 1 本訳し，.o を tmp/s17/obj へ出す
#   tcc17.sh all           11 本すべて (既にできているものは飛ばす)
#   tcc17.sh link          libtcc.a にまとめ tcc に繋ぐ (これも OS の上で)
#   tcc17.sh check         出来た tcc に実際に翻訳させる
#   tcc17.sh mk            **mk20 に tcc の Makefile を読ませて回す**
#   tcc17.sh lib           libtcc1.a を我々が作った tcc 自身で作る
#   tcc17.sh clean         tmp/s17 を消す
#
# ---- なぜ 1 本ずつ別の起動にするか ----
#
# 1 本あたり数分かかる。11 本を 1 度の起動でやると，**途中で殺された
# ときに全部やり直しになる** (docs/dev-notes.md 1.5)。1 本ごとに
# 起動を分け，出来た .o をホスト側へ取り出してスタンプを置く。
# 殺されても失うのは高々 1 本である。
#
# ---- 素材 ----
#
# docs/external/tcc (tools/fetch.sh tcc) と，そこへ patch を当てた
# tmp/tcc/src (tools/tcc.sh src)。tccdefs_.h はホストの tcc が作った
# ものを使う —— これを我々の OS の上で作るのは 16.3 の別件である。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

src=tmp/tcc/src
out=tmp/s17
root=$out/root

# Makefile の 34 行が -c する翻訳単位 (docs/stage017-cc.md 16.1)
UNITS="tccpp tccgen tccdbg tccelf tccasm tccrun riscv64-gen riscv64-link riscv64-asm libtcc tcc"

need() { [ -e "$1" ] || { echo "error: $1 が無い ($2)" >&2; exit 1; }; }

do_root() {
    need "$src" "sh tools/tcc.sh src"
    need tmp/tcc/build/tccdefs_.h "sh tools/tcc.sh host"
    for f in pp16cmd pp17 cc15qcmd ld16cmd sh2.bin cc19 kernel24.bin; do
        need "tmp/build/$f" "sh tools/build.sh stage017"
    done
    rm -rf "$root"
    mkdir -p "$root/bin" "$root/include/sys" "$root/lib" "$root/t"
    cp tmp/build/pp16cmd  "$root/bin/pp16"
    cp tmp/build/pp17     "$root/bin/pp17"
    # **名前は cc15p のまま，中身は cc15q を置く。**
    # cc19 は器の位置を "/bin/cc15p" と焼き込んでいる (stage017/cc19.c 53 行)。
    # cc19 は凍結世代なのでこの名前は変えられない。一方 cc15p は静的な
    # 初期化子の中の文字列を壊すので，tcc を cc15p で組むと tcc 自身の
    # `tcc -ar` が壊れた書庫を吐く (docs/stage017-cc.md 27〜28 章)。
    # ここは記録対象の作業場ではないので，名前と中身の対応を替えて済ませる
    #
    # STONE_CC15P はさらに差し替えて試すための穴。凍結世代を直すかどうかを
    # 決める前に「直したら通るのか」を測るためのもので，既定では使わない
    if [ -n "${STONE_CC15P:-}" ]; then
        need "$STONE_CC15P" "STONE_CC15P に置いた器"
        cp "$STONE_CC15P" "$root/bin/cc15p"
        echo "note: cc15p を $STONE_CC15P で差し替えた" >&2
    else
        cp tmp/build/cc15qcmd "$root/bin/cc15p"
    fi
    cp tmp/build/ld16cmd  "$root/bin/ld16"
    cp tmp/build/sh2.bin  "$root/bin/sh2"
    cp tmp/build/sh2.bin  "$root/sh2"
    cp tmp/build/cc19     "$root/cc19"
    cp stage017/libc20/include/*.h     "$root/include/"
    cp stage017/libc20/include/sys/*.h "$root/include/sys/"
    # **/lib は 1 揃いだけ。** cc19 は /lib/*.o を全部並べるので，
    # 鎖が繋ぐ 8 本 + rt64 + rtfp と同じにする。ctype と morecore を
    # 足すと多重定義になる。
    #
    # **libc20 を使う** (第 3 部の 3 の 3)。tcc の lib/tcov.c が
    # fcntl / getpid / EINTR を要る (docs/stage017-cc.md 27 章)
    cp tmp/build/l20_src_string.o tmp/build/l20_src_stdlib.o \
       tmp/build/l20_src_misc15.o tmp/build/l20_posix_sys.o \
       tmp/build/l20_posix_morecore.o tmp/build/l20_posix_stdio.o \
       tmp/build/l20_posix_assert.o tmp/build/l20_posix_dir.o \
       tmp/build/rt64.o tmp/build/rtfp.o "$root/lib/"
    # **木のうち .c と .h だけを載せる。** configure や .texi は要らない
    for f in "$src"/*.c "$src"/*.h "$src"/*.def; do
        [ -f "$f" ] && cp "$f" "$root/t/"
    done
    cp tmp/tcc/build/tccdefs_.h "$root/t/"
    # **tcc 自身の組み込みヘッダ。** 出来た tcc が翻訳するときに
    # CONFIG_TCCDIR/include から読む (config-stone.h は "/" なので
    # /include)。無いと "include file 'tccdefs.h' not found" で止まる
    cp "$src/include/tccdefs.h" "$root/include/"
    # **lib/ も載せる** (第 3 部の 3 の 3)。libtcc1.a を作る側である
    mkdir -p "$root/t/lib"
    for f in "$src"/lib/*.c "$src"/lib/*.S "$src"/lib/Makefile; do
        [ -f "$f" ] && cp "$f" "$root/t/lib/"
    done
    # **第 2 世代の config を使う。** 逆進 (backtrace) と境界検査を切る
    # 2 行だけ違う。RV32 では上流の tcc がそれらを作れないのに
    # lib/Makefile は作らせようとするので，その食い違いを閉じる
    # (docs/stage017-cc.md 28.6)。対になる CONFIG_backtrace=no は
    # config.mak にある —— **片方だけでは意味がない**
    cp stage017/tcc/config-stone.h "$root/t/config.h"
    echo "root: $(find "$root" -type f | wc -l) ファイル / $(du -sb "$root" | cut -f1) バイト" >&2
}

# その 1 本の入力が前と同じなら飛ばす
stampkey() {
    sha256sum "$src/$1.c" "$root/t/config.h" "$root/t/tccdefs_.h" \
        tmp/build/cc19 tmp/build/pp17 "${STONE_CC15P:-tmp/build/cc15qcmd}" tmp/build/ld16cmd \
        tmp/build/kernel24.bin 2> /dev/null | sha256sum | cut -d' ' -f1
}

do_unit() {
    u=$1
    mkdir -p "$out/obj"
    # **印を取る前に材料を揃える。** stampkey は $root/t/config.h と
    # $root/t/tccdefs_.h も混ぜる。clean の直後に unit を名指しで呼ぶと
    # それらがまだ無く，sha256sum の苦情は捨てられて **入力の一部が
    # 抜けた印**ができてしまう。次に同じことをすると印が変わり，
    # QEMU を回し直すことになる
    [ -d "$root" ] || do_root
    key=$(stampkey "$u")
    if [ -z "${STONE_FORCE_TCC17:-}" ] && [ -s "$out/obj/$u.o" ] \
        && [ "$(cat "$out/step-$u.stamp" 2> /dev/null)" = "$key" ]; then
        echo "cached $u ($out/obj/$u.o)" >&2
        return 0
    fi
    # **作り直すなら root も作り直す。** ここが `[ -d "$root" ] || do_root`
    # だけだったせいで，STONE_CC15P で器を差し替えても古い root の
    # bin/cc15p がそのまま使われ，「差し替えたのに何も変わらない」という
    # 測り違いをした。do_link で同じ誤りを直した (「在れば使う」をやめた)
    # のに，こちらを見落としていた —— 1 か所で起きるものは他でも起きる。
    # 上の「印を取る前」の呼出しとは役割が違う (あちらは材料を揃えるため，
    # こちらは古いものを捨てるため)。写すだけなので数秒である
    do_root
    # tcc.c と libtcc.c だけ -DONE_SOURCE=0 が要る (16.1 の 34 行のとおり)
    d=
    case $u in tcc|libtcc) d=" -D ONE_SOURCE=0" ;; esac
    printf 'cc19 -c t/%s.c -o t/%s.o -I t%s\necho "rc $?"\n' "$u" "$u" "$d" \
        > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/$u.log" 2>&1 || true
    # 走った後の像から .o を取り出す
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if grep -q '^rc 0$' "$out/$u.log" && [ -s "$out/back/t/$u.o" ]; then
        cp "$out/back/t/$u.o" "$out/obj/$u.o"
        echo "$key" > "$out/step-$u.stamp"
        echo "built $u ($(wc -c < "$out/obj/$u.o") バイト)" >&2
        return 0
    fi
    echo "FAIL $u ($out/$u.log):" >&2
    sed -n '1,12p' "$out/$u.log" >&2
    return 1
}

# 34 行の残り 2 手 (16.1)。
#
#   ar rcs libtcc.a libtcc.o tccpp.o ... riscv64-asm.o
#   cc -o tcc tcc.o libtcc.a ...
#
# **これも OS の上でやる。** ホストの ar / ld を使ったら，我々の OS の
# 上で組めたことにならない。
LIBOBJS="libtcc tccpp tccgen tccdbg tccelf tccasm tccrun riscv64-gen riscv64-link riscv64-asm"

do_link() {
    # **毎回組み直す。** 「在れば使う」にしていたら，root の作り方を
    # 直したのに古い root が使われ，lib/ が空のままリンクが落ちた。
    # 組み直しは写すだけで数秒である
    do_root
    for u in $UNITS; do
        [ -s "$out/obj/$u.o" ] || { echo "error: $out/obj/$u.o が無い (先に all)" >&2; exit 1; }
    done
    need tmp/build/ar17 "sh tools/build.sh stage017"
    rm -rf "$root/t"/*.o
    for u in $UNITS; do cp "$out/obj/$u.o" "$root/t/$u.o"; done
    cp tmp/build/ar17 "$root/ar"
    _mem=""
    for u in $LIBOBJS; do _mem="$_mem t/$u.o"; done
    {
        printf 'ar rcs t/libtcc.a%s\n' "$_mem"
        printf 'echo "ar $?"\n'
        printf 'cc19 -o tcc t/tcc.o t/libtcc.a\n'
        printf 'echo "link $?"\n'
        printf 'tcc -v\n'
        printf 'echo "run $?"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/link.log" 2>&1 || true
    cat "$out/link.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if [ -s "$out/back/tcc" ]; then
        cp "$out/back/tcc" "$out/tcc"
        [ -s "$out/back/t/libtcc.a" ] && cp "$out/back/t/libtcc.a" "$out/libtcc.a"
        echo "built $out/tcc ($(wc -c < "$out/tcc") バイト)" >&2
    else
        echo "FAIL: tcc ができていない ($out/link.log)" >&2
        return 1
    fi
}

# 出来た tcc に実際に翻訳させる。**-v が通っただけでは動くと言えない。**
do_check() {
    [ -s "$out/tcc" ] || { echo "error: $out/tcc が無い (先に link)" >&2; exit 1; }
    do_root
    cp "$out/tcc" "$root/tcc"
    printf 'int main(void) { return 7; }\n' > "$root/hello.c"
    {
        printf 'tcc -v\necho "v $?"\n'
        printf 'tcc -c hello.c -o hello.o -B/ -I/include\necho "compile $?"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-1800} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/check.log" 2>&1 || true
    cat "$out/check.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if [ -s "$out/back/hello.o" ]; then
        cp "$out/back/hello.o" "$out/hello.o"
        echo "我々の OS の上で組んだ tcc が hello.o を出した ($(wc -c < "$out/hello.o") バイト)" >&2
    else
        echo "FAIL: hello.o ができていない ($out/check.log)" >&2
        return 1
    fi
}

# **mk20 に tcc の Makefile を読ませて回す** (第 3 部の 3 の 2 の完了条件)。
#
# 木は t/ の中に置く。像の根に置くと，tcc の include/ が我々の
# /include を隠してしまう —— cc19 は /include/*.h を束ねるので，
# そこが tcc のものに替わると自分の libc のヘッダを見失う
# (tools/tcc-stone.sh の「平らな名前空間」の註と同じ話)。
#
# t/ で走らせる以上，素の名前は t/ からしか引けない (探す道は無い。
# 16.3)。cc19 / ar / mk を t/ にも置く。
mk_scaffold() {
    do_root
    need tmp/build/ar17 "sh tools/build.sh stage017"
    need tmp/build/mk20 "sh tools/build.sh stage017"
    cp tmp/build/cc19 "$root/t/cc19"
    cp tmp/build/ar17 "$root/t/ar"
    cp tmp/build/mk20 "$root/t/mk"
    cp tmp/build/mk20 "$root/mk"
    cp "$src/Makefile" "$root/t/Makefile"
    [ -d "$src/include" ] && mkdir -p "$root/t/include" \
        && cp "$src"/include/*.h "$root/t/include/"
    # **libc のヘッダも t/include へ置く。** lib/ の命令は -B.. なので，
    # 出来た tcc はそこを sysinclude として見る (CONFIG_TCCDIR の / では
    # なくなる)。tcov.c が <stdio.h> を読む。
    #
    # **tcc 自身のものを上書きしない。** stdarg.h / stddef.h は tcc の
    # ものでなければならない —— 我々の stdarg.h は cc 専用の隠しローカル
    # を使うので，tcc が訳すときには通らない
    # (tools/tcc-stone.sh の「平らな名前空間」の註)
    for f in stage017/libc20/include/*.h; do
        b=$(basename "$f")
        [ -f "$root/t/include/$b" ] || cp "$f" "$root/t/include/$b"
    done
    mkdir -p "$root/t/include/sys"
    for f in stage017/libc20/include/sys/*.h; do
        b=$(basename "$f")
        [ -f "$root/t/include/sys/$b" ] || cp "$f" "$root/t/include/sys/$b"
    done
    # **第 2 世代の config を使う。** 逆進 (backtrace) と境界検査を切る
    # 2 行だけ違う。RV32 では上流の tcc がそれらを作れないのに
    # lib/Makefile は作らせようとするので，その食い違いを閉じる
    # (docs/stage017-cc.md 28.6)。対になる CONFIG_backtrace=no は
    # config.mak にある —— **片方だけでは意味がない**
    cp stage017/tcc/config-stone.h "$root/t/config.h"
    cat > "$root/t/config.mak" <<'CFEOF'
# 我々の OS 向けの config.mak (docs/stage017-cc.md 21 章)。
# 上流の configure はホストでしか動かないので手で固定する
CC=cc19
CC_NAME=cc19
GCC_MAJOR=0
GCC_MINOR=0
AR=ar
LIBSUF=.a
EXESUF=
DLLSUF=.so
CFLAGS=
LDFLAGS=
LIBS=
ARCH=riscv32
TARGETOS=stone
# **TOP は書かない。** lib/Makefile は TOP = .. を置いてから
# $(TOP)/Makefile を取り込み，その中で config.mak が読まれる。
# ここで TOP=. と書くと lib/ の TOP を潰し，命令が ./tcc になって
# -B の引数が空になる (docs/stage017-cc.md 22.3)。
# TOPSRC は上流の configure が書くものなので，ここで与える
TOPSRC=$(TOP)
CONFIG_riscv32=yes
# 逆進と境界検査を作らない。config-stone.h の CONFIG_TCC_BACKTRACE 0 /
# CONFIG_TCC_BCHECK 0 と対になる (上流の configure が
# --config-backtrace=no で書く 2 か所と同じ)。これが無いと
# lib/Makefile が bt-exe.o を作らせ，型が無いまま翻訳して落ちる
CONFIG_backtrace=no
CONFIG_bcheck=no
prefix=/
bindir=/
tccdir=/
libdir=/
includedir=/include
CFEOF
}

do_mk() {
    mk_scaffold
    rm -f "$root/t"/*.o "$root/t/libtcc.a" "$root/t/tcc"
    {
        printf 'mk -C t -n tcc\necho "dry $?"\n'
        printf 'mk -C t tcc\necho "mk $?"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/mk.log" 2>&1 || true
    cat "$out/mk.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if [ -s "$out/back/t/tcc" ]; then
        cp "$out/back/t/tcc" "$out/tcc-mk"
        echo "Makefile から tcc ができた ($(wc -c < "$out/tcc-mk") バイト)" >&2
    else
        echo "FAIL: t/tcc ができていない ($out/mk.log)" >&2
        return 1
    fi
}

# **libtcc1.a を我々の OS の上で作る** (第 3 部の 3 の 3)。
#
# ここで使う翻訳器は cc19 ではなく **我々が作った tcc 自身**である
# (lib/Makefile の $(TCC) = ../tcc)。.S が 3 本あり，tcc 自身の
# アセンブラを通る —— 我々が訳した riscv64-asm.o がここで初めて
# 本気で使われる (docs/stage017-cc.md 22.2)。
do_lib() {
    # **どちらの道で作った tcc でもよい。** Makefile から回したもの
    # (tcc-mk) を優先する —— そちらが本筋だからである。
    # 名前を 1 つに決め打ちして「無い」と言うのは，前に検査でも
    # 踏んだ形である (docs/stage017-cc.md 21 章の註)
    # ただし **古い方を掴んではいけない。** tcc-mk を無条件に優先して
    # いたせいで，link で作り直した新しい tcc があるのに古い tcc-mk で
    # libtcc1.a を作り，「直したのに直っていない」と読み違えた。
    # 両方あるときは新しい方を採る
    _tcc=
    [ -s "$out/tcc-mk" ] && _tcc=$out/tcc-mk
    if [ -s "$out/tcc" ]; then
        if [ -z "$_tcc" ] || [ "$out/tcc" -nt "$_tcc" ]; then _tcc=$out/tcc; fi
    fi
    [ -n "$_tcc" ] || {
        echo "error: $out/tcc-mk も $out/tcc も無い (先に mk か link)" >&2
        exit 1
    }
    echo "note: $_tcc を使う" >&2
    mk_scaffold
    cp "$_tcc" "$root/t/tcc"
    rm -f "$root/t/lib"/*.o "$root/t/libtcc1.a"
    {
        printf 'mk -C t/lib -n\necho "dry $?"\n'
        printf 'mk -C t/lib\necho "lib $?"\n'
        # **出来た書庫を我々自身の ar で読み直す。** ファイルが出たことと
        # 書庫になっていることは別で，実際に員の見出しが 2 進数のまま
        # 50,412 バイトのファイルが出ていた (docs/stage017-cc.md 27〜28 章)。
        # ホストの ar ではなく ar17 で読む —— 同じ走行の中で済むので
        # QEMU の起動が増えない
        printf 'echo "---- ar t ----"\nt/ar t t/libtcc1.a\necho "arlist $?"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 33554432 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/lib.log" 2>&1 || true
    cat "$out/lib.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    if [ ! -s "$out/back/t/libtcc1.a" ]; then
        echo "FAIL: libtcc1.a ができていない ($out/lib.log)" >&2
        return 1
    fi
    # **壊れていても取り出す。** 読めない書庫そのものが手掛かりになる
    # (27 章はこれを見て原因に行き着いた)
    cp "$out/back/t/libtcc1.a" "$out/libtcc1.a"
    # ar17 が読めた員の並びを取っておく。テストはこれを見る
    sed -n '/^---- ar t ----$/,/^arlist /p' "$out/lib.log" \
        | sed -e '1d' -e '$d' -e '/^[[:space:]]*$/d' > "$out/libtcc1.list"
    # **ファイルが出たことを成功にしてはいけない。** 員の見出しが 2 進数の
    # まま 50,412 バイトのファイルが出ることが実際にあった (27〜28 章)。
    # ar17 が読めなければここで落とす —— 落とさないと libtcc1.list が空に
    # なり，テスト側はそれを「材料が無い」と読んで飛ばしてしまう。
    # **「材料が無い」と「壊れている」は別である**
    if ! grep -q '^arlist 0$' "$out/lib.log" || [ ! -s "$out/libtcc1.list" ]; then
        echo "FAIL: libtcc1.a を ar17 が読めない (書庫として壊れている。$out/lib.log)" >&2
        return 1
    fi
    # **lib/Makefile が最後まで通ること。** libtcc1.a の後にも作るものが
    # あり (runmain.o)，そこで止まっていては「回した」と言えない
    # (docs/stage017-cc.md 29 章)
    if ! grep -q '^lib 0$' "$out/lib.log"; then
        echo "FAIL: mk -C t/lib が最後まで通っていない ($out/lib.log)" >&2
        return 1
    fi
    echo "libtcc1.a ができた ($(wc -c < "$out/libtcc1.a") バイト / \
$(grep -c . "$out/libtcc1.list") 員)" >&2
}

case ${1:-all} in
root) do_root ;;
mk) do_mk ;;
lib) do_lib ;;
link) do_link ;;
check) do_check ;;
unit) do_unit "${2:?usage: tcc17.sh unit <名前>}" ;;
clean) rm -rf "$out" ;;
all)
    do_root
    fail=0
    for u in $UNITS; do do_unit "$u" || fail=$((fail + 1)); done
    echo "---- $(ls "$out/obj" 2>/dev/null | wc -l) / $(echo $UNITS | wc -w) 本" >&2
    [ "$fail" -eq 0 ]
    ;;
*) echo "usage: tcc17.sh [root|unit <名前>|all|link|check|mk|lib|clean]" >&2; exit 2 ;;
esac
