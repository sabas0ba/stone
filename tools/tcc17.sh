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
    for f in pp16cmd pp17 cc15scmd ld16cmd sh2.bin cc19 kernel24.bin; do
        need "tmp/build/$f" "sh tools/build.sh stage017"
    done
    rm -rf "$root"
    mkdir -p "$root/bin" "$root/include/sys" "$root/lib" "$root/t"
    cp tmp/build/pp16cmd  "$root/bin/pp16"
    cp tmp/build/pp17     "$root/bin/pp17"
    # **名前は cc15p のまま，中身は cc15s を置く。**
    # cc19 は器の位置を "/bin/cc15p" と焼き込んでいる (stage017/cc19.c 53 行)。
    # cc19 は凍結世代なのでこの名前は変えられない。一方 cc15p は静的な
    # 初期化子の中の文字列を壊すので，tcc を cc15p で組むと tcc 自身の
    # `tcc -ar` が壊れた書庫を吐く (docs/stage017-cc.md 27〜28 章)。
    # さらに cc15q は文字列リテラルの sizeof をポインタの大きさで答える
    # ので，その tcc は**書庫を読めない** (31 章)。cc15r 以降が要る。
    # いま置くのは最前線の cc15s である (33 章。tcc の出るバイト列は
    # cc15r と同じ —— tcc は多次元の char 配列を使わない)。
    # ここは記録対象の作業場ではないので，名前と中身の対応を替えて済ませる
    #
    # STONE_CC15P はさらに差し替えて試すための穴。凍結世代を直すかどうかを
    # 決める前に「直したら通るのか」を測るためのもので，既定では使わない
    if [ -n "${STONE_CC15P:-}" ]; then
        need "$STONE_CC15P" "STONE_CC15P に置いた器"
        cp "$STONE_CC15P" "$root/bin/cc15p"
        echo "note: cc15p を $STONE_CC15P で差し替えた" >&2
    else
        cp tmp/build/cc15scmd "$root/bin/cc15p"
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
# **do_root の直後にだけ呼ぶこと。** 印は $root の中身を見るので，
# 中身が「前に何をしたか」で変わってはならない (mk / lib は
# mk_scaffold で $root/t/include を足す)。
stampkey() {
    {
        sha256sum "$src/$1.c" "$root/t/config.h" "$root/t/tccdefs_.h" \
            tmp/build/cc19 tmp/build/pp17 "${STONE_CC15P:-tmp/build/cc15scmd}" \
            tmp/build/ld16cmd tmp/build/kernel24.bin
        # **libc のヘッダも数える。** cc19 は /include から読むので
        # (do_root が libc20 のものを置く)，直したら翻訳結果が変わりうる。
        # 入れていないと「直したのに作り直されない」が起きる —— この道具で
        # 何度もやった取り違えと同じ族である (docs/stage017-cc.md 28.5)。
        # 見るのは $root/include であって $root/t/include ではない
        find "$root/include" -type f | sort | xargs sha256sum
        # **tcc の木も丸ごと数える。** 単位は -I t で訳すので，共有の
        # 宣言 (tcc.h / tcctok.h / *.def) を直せば結果が変わる。さらに
        # tcc.c は tcctools.c を，libtcc.c は他を #include するので
        # 「.c は自分のぶんだけ」も足りない。**数え落としを避ける方を
        # 採る** —— 素材は patch を当て直したときにしか動かないので，
        # そのときに 11 本まとめて作り直せばよい
        find "$root/t" -maxdepth 1 -type f \
            \( -name '*.c' -o -name '*.h' -o -name '*.def' \) \
            | sort | xargs sha256sum
    } 2> /dev/null | sha256sum | cut -d' ' -f1
}

do_unit() {
    u=$1
    mkdir -p "$out/obj"
    # **毎回 root を作り直してから印を取る。** 2 つの理由がある。
    #
    #  1. 印は $root の中身を見る。作り直さないと，直前に mk や lib を
    #     走らせたかどうかで中身が変わり (mk_scaffold が t/include を
    #     足す)，同じことをしても印が変わって QEMU を回し直すことになる
    #  2. STONE_CC15P で差し替えた器を確実に置く。ここが
    #     `[ -d "$root" ] || do_root` だったせいで，差し替えても古い
    #     bin/cc15p がそのまま使われ「差し替えたのに何も変わらない」と
    #     いう測り違いをした。do_link では同じ誤りを既に直していたのに，
    #     こちらを見落としていた —— 1 か所で起きるものは他でも起きる
    #
    # 作り直しは写すだけで数秒である
    do_root
    key=$(stampkey "$u")
    if [ -z "${STONE_FORCE_TCC17:-}" ] && [ -s "$out/obj/$u.o" ] \
        && [ "$(cat "$out/step-$u.stamp" 2> /dev/null)" = "$key" ]; then
        echo "cached $u ($out/obj/$u.o)" >&2
        return 0
    fi
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
    if [ ! -s "$out/back/t/tcc" ]; then
        echo "FAIL: t/tcc ができていない ($out/mk.log)" >&2
        return 1
    fi
    cp "$out/back/t/tcc" "$out/tcc-mk"
    # **我々の c2str が作った tccdefs_.h をここで取り分ける。**
    # back は次の走行 (check や lib) で上書きされ，その root には
    # do_root が **ホストの** tccdefs_.h を写している。取り分けずに
    # back の中を見ると，**ホスト同士を比べる**ことになって検査が
    # 空回りする (レビューで指摘を受けて直した)
    if [ -s "$out/back/t/tccdefs_.h" ]; then
        cp "$out/back/t/tccdefs_.h" "$out/tccdefs_.h-mk"
    else
        echo "FAIL: t/tccdefs_.h ができていない ($out/mk.log)" >&2
        return 1
    fi
    echo "Makefile から tcc ができた ($(wc -c < "$out/tcc-mk") バイト)" >&2
}

# **tcc が結合した実行形式を我々の OS の上で走らせる** (第 5 部の 1)。
#
# ここまで tcc は `-c` までしか使っていない (do_check)。結合まで任せると
# 3 つ足りないものが出る。
#
#   1. crt1.o / crti.o / crtn.o が無い  -> stage017/crt1.S + crt1c.c を
#                                          tcc 自身に組ませて /usr/lib へ置く
#   2. 既定の載せ先が 0x0001_0000     -> -Wl,-Ttext=0x86000000
#                                          (我々の UBASE。kernel24.c)
#   3. 既定が動的リンク (INTERP を持つ) -> -static
#
# 3 つとも「我々の OS の形」であって tcc の誤りではない。**唯一の誤りは
# tcc のアセンブラの `call`** で，それは crt1.S の側で避けている
# (docs/stage017-cc.md 30 章)。
#
# 完了条件は**終了コード**で見る。tcc が組んだ像が走り，main の返り値が
# 終了コードになり，argc / argv が届いていること。
#
# **ここは -nostdlib で、crt1.o と crt1c.o を手で並べる。** この時点では
# tcc の世界の libc がまだ無く (30.6)、我々の l20_*.o は呼出し規約が
# 違うので繋げないためである。したがってこの作業が測るのは
# 「入口が正しく書けているか」までで、**駆動役が自分で crt を拾う道は
# 測っていない** —— crti.o / crtn.o も並ぶだけで効いていない。
#
# その道は do_oslibc が測る (31 章)。crt1c.o を libc.a の員に入れて
# あるので、
#
#     tcc -static -o q q.c -B/tccb -Wl,-Ttext=0x86000000
#
# と打つだけで crt1.o / crti.o / crtn.o は CRTPREFIX (/usr/lib) から、
# __start_c は -lc から自動で拾われる。**そちらが本番である。**
CRTPROG_EXPECT='p1 7
p2 1
p3 9'

do_crt() {
    do_root
    need stage017/crt1.S "リポジトリ"
    need stage017/crt1c.c "リポジトリ"
    [ -s "$out/tcc" ] || { echo "error: $out/tcc が無い (先に link)" >&2; exit 1; }
    cp "$out/tcc" "$root/tcc"
    mkdir -p "$root/usr/lib"
    cp stage017/crt1.S  "$root/crt1.S"
    cp stage017/crt1c.c "$root/crt1c.c"
    : > "$root/empty.S"
    # 7 を返す / argc を返す / argv[0] の 1 文字目を見る。
    # ./p3 で呼ぶので argv[0] は "./p3" である
    printf 'int main(int argc, char **argv) { return 7; }\n' > "$root/p1.c"
    printf 'int main(int argc, char **argv) { return argc; }\n' > "$root/p2.c"
    printf 'int main(int argc, char **argv) { return argv[0][0] == 46 ? 9 : 1; }\n' \
        > "$root/p3.c"
    {
        # **1 本ずつ数える。** まとめて $? を見ると最後の 1 本しか
        # 見ないことになる (28.5 の 4「数えているつもりで数えていない」)
        printf 'tcc -c crt1.S -o usr/lib/crt1.o -B/\necho "crt-crt1 $?"\n'
        printf 'tcc -c crt1c.c -o usr/lib/crt1c.o -B/\necho "crt-crt1c $?"\n'
        printf 'tcc -c empty.S -o usr/lib/crti.o -B/\necho "crt-crti $?"\n'
        printf 'tcc -c empty.S -o usr/lib/crtn.o -B/\necho "crt-crtn $?"\n'
        for n in p1 p2 p3; do
            printf 'tcc -static -o %s %s.c -B/ -nostdlib -Wl,-Ttext=0x86000000 usr/lib/crt1.o usr/lib/crt1c.o\n' "$n" "$n"
            printf 'echo "link-%s $?"\n' "$n"
        done
        printf 'echo "---- run ----"\n'
        for n in p1 p2 p3; do
            printf './%s\necho "%s $?"\n' "$n" "$n"
        done
        printf 'echo "---- end ----"\n'
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
        > "$out/crt.log" 2>&1 || true
    cat "$out/crt.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    # **走った結果を取り分ける。** back は次の走行で上書きされる
    # (28.5 の 5 と同じ話)
    sed -n '/^---- run ----$/,/^---- end ----$/p' "$out/crt.log" \
        | sed -e '1d' -e '$d' -e '/^[[:space:]]*$/d' > "$out/crt-run.txt"
    if [ ! -s "$out/back/usr/lib/crt1.o" ]; then
        echo "FAIL: crt1.o ができていない ($out/crt.log)" >&2
        return 1
    fi
    cp "$out/back/usr/lib/crt1.o" "$out/crt1.o"
    if [ "$(cat "$out/crt-run.txt")" != "$CRTPROG_EXPECT" ]; then
        echo "FAIL: tcc が組んだ像の走りが期待と違う ($out/crt.log)" >&2
        echo "--- 実測:" >&2; cat "$out/crt-run.txt" >&2
        echo "--- 期待:" >&2; printf '%s\n' "$CRTPROG_EXPECT" >&2
        return 1
    fi
    echo "tcc が組んだ実行形式が我々の OS の上で走った (p1=7 p2=argc p3=argv)" >&2
}

# **tcc の世界の libc を作る** (第 5 部の 2)。
#
# 30.6 の続き。我々の libc20 の .o は `cc15k` が組んだもので，引数を
# データスタック (x9) で渡す**我々の規約**に従う。tcc は標準の RISC-V
# ABI を使うので**この 2 つは互いに呼べない**。だから do_crt では
# `-nostdlib` が要った。
#
# ここでやるのは **libc20 のソースを tcc に訳し直す**ことである。
# ソースは C なので tcc が読める。`sys_*` だけは 'E' 前置部が機械語で
# 持っていて我々の規約に従うので，標準 ABI 版を stage017/syscall.S に
# 書き直して足す。
#
# 出来上がりは `/usr/lib/libc.a`。これがあると `-nostdlib` 無しで
# 結合でき，`tcc -static -o q q.c` だけで `printf` が使える。
#
# 完了条件はやはり**走らせて見る**。printf / malloc / strlen / fopen が
# 実際に働くことを、出力と終了コードで確かめる。
# 単位は **経路:員の名前** で書く。名前を短くするのは，ar の名前欄が
# 16 バイトしか無く，GNU の形式は終端に '/' を要るからである。
# posix_morecore.o はちょうど 16 文字で，我々の ar17 は 15 文字に
# 切れて見えた。**書庫の中で名前が変わる**のは避ける
OSLIBC_UNITS='src/string:string src/stdlib:stdlib src/ctype:ctype
src/misc15:misc15 posix/sys:sys posix/morecore:morecore
posix/stdio:stdio posix/assert:assert posix/dir:dir posix/signal:signal'

# **tcc の世界は libc21 を使う。** libc20 との差は 3 つで、どれも
# 「実物が読むのに我々が持っていなかったもの」である (32 章)。
#
#   include/sys/types.h   zlib の zconf.h が読む (off_t)
#   include/signal.h      bzip2 の bzip2.c が読む
#   posix/signal.c        受け付けて何も起こさない signal / 拒む raise
#
# **鎖の側 (cc19 が tcc を組む道) は libc20 のままにする。** そちらを
# 動かすと 21 章・29 章の突き合わせが動く。libc21 から作られる記録対象の
# 成果物はまだ無いので、世代を刻んでも鎖は 1 バイトも変わらない
OSLIBC_SRC=stage017/libc21

OSLIBC_EXPECT='q1 hello 5
q2 1 ./q
q3 abc
q4 ABCD
q 5'

# 測り手の答 (31.3)。**印刷するだけでは検査にならない**ので突き合わせる。
# ここが動いたら書庫が読めなくなるはずなので、退行の網でもある
OSLIBC_PROBE='v 55
szstr 9
a-bad 0'

do_oslibc() {
    # **mk_scaffold が先。** 中で do_root が走り，root を作り直す。
    # 後から呼ぶと下でここへ置いたものが全部消える。ar17 (t/ar) が要る
    mk_scaffold
    need stage017/crt1.S "リポジトリ"
    need stage017/crt1c.c "リポジトリ"
    need stage017/syscall.S "リポジトリ"
    [ -s "$out/tcc" ] || { echo "error: $out/tcc が無い (先に link)" >&2; exit 1; }
    [ -s "$out/libtcc1.a" ] || {
        echo "error: $out/libtcc1.a が無い (先に lib)" >&2; exit 1; }
    cp "$out/tcc" "$root/tcc"
    mkdir -p "$root/usr/lib" "$root/libc"
    # **tcc が自分で足す -ltcc1 の相手。** LIBPATHS は {B} と /usr/lib
    # なので、-B/tccb なら /usr/lib で引ける (tcc.h 291 行)
    cp "$out/libtcc1.a" "$root/usr/lib/libtcc1.a"
    for u in $OSLIBC_UNITS; do
        cp "$OSLIBC_SRC/${u%%:*}.c" "$root/libc/${u#*:}.c"
    done
    cp stage017/crt1.S    "$root/crt1.S"
    cp stage017/crt1c.c   "$root/libc/crt1c.c"
    cp stage017/syscall.S "$root/libc/syscall.S"
    : > "$root/empty.S"
    # **tcc に自分の -B の木を与える** (docs/stage017-cc.md 31.2)。
    #
    # do_root が置く /include は libc20 のもので，その stdarg.h は
    # **我々の cc 専用**である —— コンパイラが用意する隠しローカル
    # __va_ptr を使う (stage017/libc20/include/stdarg.h)。tcc に
    # 訳させると "'__va_ptr' undeclared" で落ちる。
    #
    # 逆に tcc の stddef.h を /include に上書きすると，今度は
    # **cc19 が読めなくなる**。1 つの /include を 2 つの処理系が
    # 共有しているのが誤りで，分けるのが答である。
    #
    #   /tccb/include  tcc のもの (stdarg.h / stddef.h / tccdefs.h …)
    #   /usr/include   libc20 のもの
    #   /include       libc20 のもの (cc19 が読む。**触らない**)
    #
    # tcc の探し順は {B}/include -> /usr/include なので (tcc.h 280 行)，
    # -B/tccb とすれば tcc のものが先に当たり，残りは libc20 から拾える。
    # 書庫の探し順は {B} -> /usr/lib なので /usr/lib で揃う
    # 木の名前は /tccb。/tcc にすると本体の実行形式 (/tcc) と
    # ぶつかる —— sfs では同じ名前の枝と葉は持てない
    mkdir -p "$root/tccb/include" "$root/usr/include/sys"
    cp "$src"/include/*.h "$root/tccb/include/"
    cp "$OSLIBC_SRC"/include/*.h     "$root/usr/include/"
    cp "$OSLIBC_SRC"/include/sys/*.h "$root/usr/include/sys/"
    # **"invalid archive" の切り分け (31.3)。**
    #
    # tcc の書庫読みが折れている。原因を「書庫の側」と「lseek / read の
    # 側」と「64 ビットの側」に分けたい。
    #
    # 測り手は **tcc で組み，-nostdlib で繋ぐ**。cc19 で組むと我々の
    # 規約の libc が混ざり，測っている対象が変わってしまう。答は
    # **終了コード**で受ける (printf が無い)。
    cat > "$root/v.c" <<'VEOF'
/* tccelf.c の full_read / read_ar_header と同じ順で当たる。
 * 位置 8 が書庫の最初の見出し，2704 が最初の員の見出しである
 * (ホストで od して確かめた値) */
int sys_openat(int dirfd, char *path, int flags, int mode);
int sys_read(int fd, void *buf, int n);
int sys_ecall(int n, int a, int b, int c);

/* tccelf.c 3225 行の full_read の写し。**要求した数で止まらず，
 * 0 が返るまで読む**ことに注意 */
static int rd(int fd, char *b, int n) {
  int rnum = 0;
  int num;
  while (1) {
    num = sys_read(fd, b + rnum, n - rnum);
    if (num < 0) return num;
    if (num == 0) return rnum;
    rnum += num;
  }
}

int main(int argc, char **argv) {
  char h[64];
  int fd;
  int r;
  fd = sys_openat(-100, "usr/lib/libtcc1.a", 0, 0);
  if (fd < 0) return 41;
  if (sys_ecall(62, fd, 8, 0) != 8) return 42;
  r = rd(fd, h, 60);
  if (r != 60) return 43;
  if (h[58] != 96 || h[59] != 10) return 44;
  if (sys_ecall(62, fd, 2704, 0) != 2704) return 45;
  r = rd(fd, h, 60);
  if (r != 60) return 46;
  if (h[58] != 96 || h[59] != 10) return 47;
  /* 64 ビットの桁送り (get_be)。ここが狂うと員の位置が出鱈目になる */
  {
    unsigned long long v = 0;
    unsigned char a[4];
    int i;
    a[0] = 0; a[1] = 0; a[2] = 10; a[3] = 144;   /* 2704 */
    for (i = 0; i < 4; i++) v = (v << 8) | a[i];
    if (v != 2704) return 48;
  }
  return 55;
}
VEOF
    # **同じことを cc19 でも測る。** 上の v.c は tcc が訳したものだが，
    # 折れているのは **cc19 (= cc15s) が訳した tcc** の側かもしれない。
    # 器が違えば測っている対象が違う —— 両方要る
    cat > "$root/pr.c" <<'PREOF'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
typedef struct ArchiveHeader {
  char ar_name[16];
  char ar_date[12];
  char ar_uid[6];
  char ar_gid[6];
  char ar_mode[8];
  char ar_size[10];
  char ar_fmag[2];
} ArchiveHeader;
int sys_ecall(int n, int a, int b, int c);
int main(int argc, char **argv) {
  ArchiveHeader hdr;
  ArchiveHeader *h;
  char *p;
  char *e;
  int fd;
  int r;
  int size;
  h = &hdr;
  printf("sz-hdr %d\n", (int)sizeof(ArchiveHeader));
  printf("sz-name %d\n", (int)sizeof h->ar_name);
  printf("sz-size %d\n", (int)sizeof h->ar_size);
  printf("sz-fmag %d\n", (int)sizeof h->ar_fmag);
  fd = open("usr/lib/libtcc1.a", 0, 0);
  r = sys_ecall(62, fd, 8, 0);
  printf("seek %d\n", r);
  r = read(fd, h, 60);
  printf("hdr-read %d\n", r);
  r = memcmp(h->ar_fmag, "`\n", 2);
  printf("fmag %d %d %d\n", (int)h->ar_fmag[0], (int)h->ar_fmag[1], r);
  p = h->ar_name;
  e = p + sizeof h->ar_name;
  while (e > p && e[-1] == 32) e = e - 1;
  *e = 0;
  printf("name [%s] cmp %d\n", p, strcmp(p, "/"));
  h->ar_size[sizeof h->ar_size - 1] = 0;
  size = (int)strtol(h->ar_size, 0, 0);
  printf("size %d\n", size);
  close(fd);
  return 0;
}
PREOF
    # **64 ビットの値を int の仮引数へ渡す形を当たる。**
    # tccelf.c は off (unsigned long long) を read_ar_header(int offset)
    # へ渡す。我々の規約は引数をデータスタックへ積むので，型どおりに
    # 1 語へ縮めずに 2 語積むと**以降の引数が全部ずれる**。
    # 静かに誤る形なので，翻訳が通ったことでは判らない
    cat > "$root/pr2.c" <<'P2EOF'
#include <stdio.h>
/* tccelf.c 3595 行の写し。コンマ演算子も込みで写す */
unsigned long long get_be(unsigned char *b, int n) {
  unsigned long long ret = 0;
  while (n)
    ret = (ret << 8) | *b++, --n;
  return ret;
}
int three(int a, int b, char *p) { return a + b + (p != 0); }
int main(int argc, char **argv) {
  unsigned char x[4];
  unsigned long long v;
  int n;
  char c;
  x[0] = 0;
  x[1] = 0;
  x[2] = 0;
  x[3] = 116;
  v = get_be(x, 4);
  n = (int)v;
  printf("gb1 %d\n", n);
  x[2] = 10;
  x[3] = 144;
  v = get_be(x, 4);
  n = (int)v;
  printf("gb2 %d\n", n);
  printf("arg32 %d\n", three(1, n, &c));
  v = 5;
  printf("arg64 %d\n", three(1, v, &c));
  /* **文字列リテラルの sizeof。** tccelf.c 3681 行は
   *   file_offset = sizeof ARMAG - 1;    ARMAG = "!<arch>\\n"
   * と書く。C では char[9] なので 8 でなければならない */
  printf("szstr %d\n", (int)sizeof "!<arch>\n");
  printf("szstr2 %d\n", (int)sizeof("abc"));
  return 0;
}
P2EOF
    # **tcc_load_alacarte の前半をそのまま写して当たる (31.3)。**
    # ここまでの測りは部品を 1 つずつ見てきたが，どれも通った。
    # 通らないのは組み合わせなので，**同じ順で同じことをする**手を書く
    cat > "$root/pr3.c" <<'P3EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
int sys_ecall(int n, int a, int b, int c);
unsigned long long get_be(unsigned char *b, int n) {
  unsigned long long ret = 0;
  while (n)
    ret = (ret << 8) | *b++, --n;
  return ret;
}
/* tccelf.c 3225 行の full_read の写し */
int fread_all(int fd, void *buf, int count) {
  char *cbuf = buf;
  int rnum = 0;
  int num;
  while (1) {
    num = read(fd, cbuf, count - rnum);
    if (num < 0) return num;
    if (num == 0) return rnum;
    rnum = rnum + num;
    cbuf = cbuf + num;
  }
}
int main(int argc, char **argv) {
  int fd;
  int size;
  int nsyms;
  int i;
  int bad;
  int len;
  unsigned long long off;
  unsigned char *data;
  unsigned char *ar_index;
  char *ar_names;
  char *p;
  char h[64];
  fd = open("usr/lib/libtcc1.a", 0, 0);
  sys_ecall(62, fd, 8, 0);
  len = fread_all(fd, h, 60);
  h[57] = 0;
  size = (int)strtol(h + 48, 0, 0);
  printf("a-size %d %d\n", size, len);
  data = (unsigned char *)malloc(size);
  printf("a-malloc %d\n", data != 0);
  len = fread_all(fd, data, size);
  printf("a-idx %d\n", len);
  nsyms = (int)get_be(data, 4);
  printf("a-nsyms %d\n", nsyms);
  ar_index = data + 4;
  ar_names = (char *)ar_index + nsyms * 4;
  bad = 0;
  p = ar_names;
  for (i = 0; i < nsyms; i++) {
    off = get_be(ar_index + i * 4, 4);
    sys_ecall(62, fd, (int)off, 0);
    if (fread_all(fd, h, 60) != 60) bad = bad + 1;
    else if (h[58] != 96 || h[59] != 10) bad = bad + 1;
    p = p + strlen(p) + 1;
  }
  printf("a-bad %d\n", bad);
  printf("a-first [%s]\n", ar_names);
  printf("a-end %d\n", (int)(p - (char *)data));
  free(data);
  close(fd);
  return 0;
}
P3EOF
    # printf / malloc / strlen / fopen を 1 本で当たる。
    # 返り値は strlen("hello") = 5。argv[0] は "./q" で呼ぶので "./q"
    cat > "$root/q.c" <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  char *p;
  char b[16];
  FILE *f;
  int n;
  p = malloc(64);
  strcpy(p, "hello");
  n = strlen(p);
  printf("q1 %s %d\n", p, n);
  printf("q2 %d %s\n", argc, argv[0]);
  free(p);
  f = fopen("q.txt", "w");
  if (f == 0) return 91;
  fprintf(f, "abc\n");
  fclose(f);
  f = fopen("q.txt", "r");
  if (f == 0) return 92;
  b[0] = 0;
  fgets(b, 16, f);
  fclose(f);
  printf("q3 %s", b);
  /* **追記が本当に末尾へ書くか** (32 章)。第 20 世代までの fopen は
     "a" でも先頭から上書きしていた。ABCD にならなければ壊れている */
  f = fopen("q4.txt", "w");
  if (f == 0) return 93;
  fputs("AB", f);
  fclose(f);
  f = fopen("q4.txt", "a");
  if (f == 0) return 94;
  fputs("CD", f);
  fclose(f);
  f = fopen("q4.txt", "r");
  if (f == 0) return 95;
  b[0] = 0;
  fgets(b, 16, f);
  fclose(f);
  printf("q4 %s\n", b);
  return n;
}
CEOF
    _objs=
    {
        # **1 本ずつ数える。** どの単位が通らないかが判らないと
        # 「libc が組めない」で終わってしまう
        for u in $OSLIBC_UNITS; do
            n=${u#*:}
            printf 'tcc -c libc/%s.c -o libc/%s.o -B/tccb\necho "u-%s $?"\n' "$n" "$n" "$n"
        done
        printf 'tcc -c libc/syscall.S -o libc/syscall.o -B/tccb\necho "u-syscall $?"\n'
        printf 'tcc -c libc/crt1c.c -o libc/crt1c.o -B/tccb\necho "u-crt1c $?"\n'
        printf 'tcc -c crt1.S -o usr/lib/crt1.o -B/tccb\necho "u-crt1 $?"\n'
        printf 'tcc -c empty.S -o usr/lib/crti.o -B/tccb\necho "u-crti $?"\n'
        printf 'tcc -c empty.S -o usr/lib/crtn.o -B/tccb\necho "u-crtn $?"\n'
        printf 'tcc -ar rcs usr/lib/libc.a'
        for u in $OSLIBC_UNITS; do
            printf ' libc/%s.o' "${u#*:}"
        done
        printf ' libc/syscall.o libc/crt1c.o\necho "ar $?"\n'
        # **書庫として読めるか。** ファイルが出たことは成功ではない
        # (28 章)。我々自身の ar17 で読み直す
        printf 'echo "---- ar t ----"\nt/ar t usr/lib/libc.a\necho "arlist $?"\n'
        # 書庫の読みの切り分け (31.3)。55 なら全部通っている
        printf 'tcc -static -o v v.c -B/tccb -nostdlib -Wl,-Ttext=0x86000000'
        printf ' usr/lib/crt1.o libc/crt1c.o libc/syscall.o\necho "v-ld $?"\n'
        printf './v\necho "v $?"\n'
        printf 'cc19 -c pr.c -o pr.o\necho "pr-cc $?"\n'
        printf 'cc19 -o pr pr.o\necho "pr-ld $?"\n./pr\necho "pr $?"\n'
        printf 'cc19 -c pr2.c -o pr2.o\necho "pr2-cc $?"\n'
        printf 'cc19 -o pr2 pr2.o\necho "pr2-ld $?"\n./pr2\necho "pr2 $?"\n'
        printf 'cc19 -c pr3.c -o pr3.o\necho "pr3-cc $?"\n'
        printf 'cc19 -o pr3 pr3.o\necho "pr3-ld $?"\n./pr3\necho "pr3 $?"\n'
        printf 'tcc -static -o q q.c -B/tccb -Wl,-Ttext=0x86000000\necho "link-q $?"\n'
        printf 'echo "---- run ----"\n./q\necho "q $?"\necho "---- end ----"\n'
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
        > "$out/oslibc.log" 2>&1 || true
    cat "$out/oslibc.log"
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    sed -n '/^---- run ----$/,/^---- end ----$/p' "$out/oslibc.log" \
        | sed -e '1d' -e '$d' -e '/^[[:space:]]*$/d' > "$out/oslibc-run.txt"
    sed -n '/^---- ar t ----$/,/^arlist /p' "$out/oslibc.log" \
        | sed -e '1d' -e '$d' -e '/^[[:space:]]*$/d' > "$out/libc.list"
    if [ ! -s "$out/back/usr/lib/libc.a" ]; then
        echo "FAIL: libc.a ができていない ($out/oslibc.log)" >&2
        return 1
    fi
    cp "$out/back/usr/lib/libc.a" "$out/libc.a"
    if ! grep -q '^arlist 0$' "$out/oslibc.log" || [ ! -s "$out/libc.list" ]; then
        echo "FAIL: libc.a を ar17 が読めない (書庫として壊れている。$out/oslibc.log)" >&2
        return 1
    fi
    # 測り手の答を取り分けて突き合わせる (31.3)
    grep -E '^(v|szstr|a-bad) ' "$out/oslibc.log" > "$out/oslibc-probe.txt"
    if [ "$(cat "$out/oslibc-probe.txt")" != "$OSLIBC_PROBE" ]; then
        echo "FAIL: 測り手の答が期待と違う ($out/oslibc.log)" >&2
        echo "--- 実測:" >&2; cat "$out/oslibc-probe.txt" >&2
        echo "--- 期待:" >&2; printf '%s\n' "$OSLIBC_PROBE" >&2
        return 1
    fi
    if [ "$(cat "$out/oslibc-run.txt")" != "$OSLIBC_EXPECT" ]; then
        echo "FAIL: libc.a で組んだ像の走りが期待と違う ($out/oslibc.log)" >&2
        echo "--- 実測:" >&2; cat "$out/oslibc-run.txt" >&2
        echo "--- 期待:" >&2; printf '%s\n' "$OSLIBC_EXPECT" >&2
        return 1
    fi
    echo "tcc の世界の libc ができた ($(wc -c < "$out/libc.a") バイト / \
$(grep -c . "$out/libc.list") 員)。-nostdlib 無しで printf が使える" >&2
}

# **tcc より一段大きい実物を，我々の OS の上で tcc に組ませる**
# (stage017-gcc.md 5.2)。
#
# 狙いは「何が足りないかを数える」ことである。Stage 16 の教訓は
# 「律速はコンパイラではなく OS だった」なので (stage016-os.md 1〜4 章)，
# GCC の素材が無いうちに，手元にある一段大きい実物で同じことを測る。
#
# Stage 14 は同じ 2 つを**ホストの鎖**で訳した (tests/stage014 第 8〜9 部)。
# ここは **我々の OS の上で tcc が**訳す。退行の検査にもなる。
#
# **1 単位ずつ終了コードを数える。** どこまで通ってどこで止まるかが
# 判らないと「足りないものの表」が書けない。
#
# **完了条件は走らせて見る。** ここは「組めた段の数」ではなく
# `---- run ----` の塊がそのまま合うことで見る —— 段ごとの数え上げだけに
# すると、走っていないのに通ったように見える行が混ざる。実際
# `diff seed.txt seed.orig` は **bzip2 が動かなければ必ず 0 を返す**。
EXT_EXPECT='compress: 4096 -> 2555 sum=59847
roundtrip ok
bz-rt 0
zex 0'

BZ_UNITS='blocksort huffman crctable randtable compress decompress bzlib'
Z_UNITS='adler32 compress crc32 deflate infback inffast inflate inftrees
trees uncompr zutil gzclose gzlib gzread gzwrite'

do_ext() {
    mk_scaffold
    need "$out/tcc" "sh tools/tcc17.sh link"
    need "$out/libc.a" "sh tools/tcc17.sh oslibc"
    need "$out/libtcc1.a" "sh tools/tcc17.sh lib"
    need docs/external/bzip2 "sh tools/fetch.sh bzip2"
    need docs/external/zlib "sh tools/fetch.sh zlib"
    need stage017/crt1.S "リポジトリ"
    need stage017/crt1c.c "リポジトリ"
    need stage017/syscall.S "リポジトリ"
    cp "$out/tcc" "$root/tcc"
    mkdir -p "$root/usr/lib" "$root/tccb/include" "$root/usr/include/sys" \
             "$root/bz" "$root/z"
    cp "$out/libc.a"    "$root/usr/lib/libc.a"
    cp "$out/libtcc1.a" "$root/usr/lib/libtcc1.a"
    cp "$src"/include/*.h "$root/tccb/include/"
    cp "$OSLIBC_SRC"/include/*.h     "$root/usr/include/"
    cp "$OSLIBC_SRC"/include/sys/*.h "$root/usr/include/sys/"
    # crt は毎回この場で組む (libc.a と揃っている必要がある)
    cp stage017/crt1.S "$root/crt1.S"
    : > "$root/empty.S"
    for f in docs/external/bzip2/*.c docs/external/bzip2/*.h; do
        cp "$f" "$root/bz/"
    done
    for f in docs/external/zlib/*.c docs/external/zlib/*.h; do
        cp "$f" "$root/z/"
    done
    cp docs/external/zlib/test/example.c "$root/z/zex.c"
    # libbz2 の往復検査。**Stage 14 が我々の鎖で組んだのと同じ本**を
    # 使う (tests/stage014/user/bzt.c)。同じ答が出れば退行の検査になる
    need tests/stage014/user/bzt.c "リポジトリ"
    cp tests/stage014/user/bzt.c "$root/bz/bzt.c"
    {
        printf 'tcc -c crt1.S -o usr/lib/crt1.o -B/tccb\necho "crt $?"\n'
        printf 'tcc -c empty.S -o usr/lib/crti.o -B/tccb\n'
        printf 'tcc -c empty.S -o usr/lib/crtn.o -B/tccb\n'
        printf 'echo "---- bz ----"\n'
        for u in $BZ_UNITS; do
            printf 'tcc -c bz/%s.c -o bz/%s.o -B/tccb -I bz\necho "bz-%s $?"\n' \
                "$u" "$u" "$u"
        done
        printf 'tcc -ar rcs usr/lib/libbz2.a'
        for u in $BZ_UNITS; do printf ' bz/%s.o' "$u"; done
        printf '\necho "bz-ar $?"\n'
        # 往復検査の本。**これが libbz2 が使えることの証拠**である
        printf 'tcc -static -o bzt bz/bzt.c -B/tccb -I bz'
        printf ' -Wl,-Ttext=0x86000000 usr/lib/libbz2.a\necho "bz-drv $?"\n'
        # bzip2 の**命令そのもの**。ここは通らない見込みで、通らない
        # 理由を数えるために回す (32 章)
        printf 'tcc -static -o bzip2 bz/bzip2.c -B/tccb -I bz'
        printf ' -Wl,-Ttext=0x86000000 usr/lib/libbz2.a\necho "bz-prog $?"\n'
        printf 'echo "---- z ----"\n'
        for u in $Z_UNITS; do
            printf 'tcc -c z/%s.c -o z/%s.o -B/tccb -I z\necho "z-%s $?"\n' \
                "$u" "$u" "$u"
        done
        printf 'tcc -ar rcs usr/lib/libz.a'
        for u in $Z_UNITS; do printf ' z/%s.o' "$u"; done
        printf '\necho "z-ar $?"\n'
        printf 'tcc -static -o zex z/zex.c -B/tccb -I z'
        printf ' -Wl,-Ttext=0x86000000 usr/lib/libz.a\necho "z-prog $?"\n'
        # **走らせる。** 組めたことは完了条件ではない (28 章)
        printf 'echo "---- run ----"\n'
        printf './bzt\necho "bz-rt $?"\n'
        # zex はよく喋るので，出力は別に取る。見るのは終了コード
        printf './zex > zex.out\necho "zex $?"\n'
        printf 'echo "---- end ----"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 67108864 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/ext.log" 2>&1 || true
    cat "$out/ext.log"
    # **走った結果を取り分ける。** zex はよく喋るので出力を別に取って
    # あり (zex.out)、そちらが「何を確かめたか」の証拠になる
    dd if="$out/ram" of="$out/back.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/back"
    sh tools/sfs3.sh unpack "$out/back.img" "$out/back" > /dev/null 2>&1 || true
    cp "$out/back/zex.out" "$out/zex.out" 2> /dev/null || : > "$out/zex.out"
    [ -s "$out/back/usr/lib/libz.a" ] && cp "$out/back/usr/lib/libz.a" "$out/libz.a"
    [ -s "$out/back/usr/lib/libbz2.a" ] && cp "$out/back/usr/lib/libbz2.a" "$out/libbz2.a"
    sed -n '/^---- run ----$/,/^---- end ----$/p' "$out/ext.log" \
        | sed -e '1d' -e '$d' -e '/^[[:space:]]*$/d' > "$out/ext-run.txt"
    # **足りないものの表。** 通った数ではなく，通らなかった段とその理由を出す
    grep -E '^(bz|z)-[a-z0-9]+ [^0]' "$out/ext.log" > "$out/ext-bad.txt" || true
    echo "---- 通らなかった段: $(grep -c . "$out/ext-bad.txt") ----" >&2
    grep -oE "include file '[^']+' not found" "$out/ext.log" | sort -u >&2 || true
    if [ "$(cat "$out/ext-run.txt")" != "$EXT_EXPECT" ]; then
        echo "FAIL: zlib / bzip2 がまだ走らない ($out/ext.log)" >&2
        echo "--- 実測:" >&2; cat "$out/ext-run.txt" >&2
        echo "--- 期待:" >&2; printf '%s\n' "$EXT_EXPECT" >&2
        return 1
    fi
    if ! grep -q '^zlib version 1\.3\.1 ' "$out/zex.out"; then
        echo "FAIL: zex の出力が取れていない ($out/zex.out)" >&2
        return 1
    fi
    echo "zlib ($(wc -c < "$out/libz.a") バイト) と libbz2 \
($(wc -c < "$out/libbz2.a") バイト) が我々の OS の上で tcc に組まれ，走った" >&2
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
crt) do_crt ;;
oslibc) do_oslibc ;;
ext) do_ext ;;
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
*) echo "usage: tcc17.sh [root|unit <名前>|all|link|check|mk|lib|crt|oslibc|ext|clean]" >&2; exit 2 ;;
esac
