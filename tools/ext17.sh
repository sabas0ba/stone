#!/bin/sh
# 実物 (zlib / bzip2) を **我々の器で，我々の OS の上で** 組む
# (docs/stage017-gcc.md 5.1)。
#
# Stage 14 はこの 2 つを**ホストの鎖**で訳した (tests/stage014 第 8〜9 部)。
# ここは **自作 OS の上で自作の器 (cc19 + cc15v) が**訳す。それが一段先の
# 指標になる。
#
# **外部の器は使わない。** 一度 tcc に組ませたが，それでは tcc の成熟度を
# 測ることになり我々の値が出ない (docs/artifacts.md 3 章 /
# docs/stage017-cc.md 34 章)。的を道具に使わない。
#
#   sh tools/ext17.sh probe   1 単位ずつ訳して，通らなかったものを数える
#   sh tools/ext17.sh run     書庫にまとめ，駆動を繋いで**実際に走らせる**
#                             (我々が書いた駆動と，**zlib 自身の検査**の両方)
#   sh tools/ext17.sh clean
#
# **訳せることと動くことは別である。** 22/22 訳せても，それは「構文を
# 拒まなかった」でしかない。run は出来た .o を我々の ar でまとめ，
# 我々の cc19 で繋ぎ，我々の OS の上で圧縮・伸長させて**元に戻ること**を
# 見る。
#
# 見るのは「何単位通ったか」ではなく **通らなかった単位とその理由**である。
# tcc のときはそれが「C 適合の誤り 4 つ + libc の穴 6 つ」という表になった。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/e17
root=$out/root
mkdir -p "$out"

# libc の世代。**ヘッダは libc21** —— 実物のソースを読んで判った穴を
# 埋めたもの (sys/types.h / signal.h ほか。stage017/libc21.md)
LIBC=stage017/libc21

Z_UNITS='adler32 compress crc32 deflate infback inffast inflate inftrees
trees uncompr zutil gzclose gzlib gzread gzwrite'
BZ_UNITS='blocksort huffman crctable randtable compress decompress bzlib'

need() {
    [ -e "$1" ] && return 0
    echo "error: $1 が無い ($2)" >&2
    exit 1
}

do_root() {
    for f in pp16cmd pp17 cc15vcmd ld17cmd sh2.bin cc19 ar17 kernel24.bin; do
        need "tmp/build/$f" "sh tools/build.sh stage017"
    done
    need docs/external/zlib  "sh tools/fetch.sh zlib"
    need docs/external/bzip2 "sh tools/fetch.sh bzip2"
    rm -rf "$root"
    mkdir -p "$root/bin" "$root/include/sys" "$root/lib" "$root/z" "$root/bz"
    cp tmp/build/pp16cmd  "$root/bin/pp16"
    cp tmp/build/pp17     "$root/bin/pp17"
    # cc19 は器の位置を "/bin/cc15p" と焼き込んでいる (cc19.c 53 行)。
    # 凍結世代なので名前は変えられない。中身は最前線の cc15v を置く
    cp tmp/build/cc15vcmd "$root/bin/cc15p"
    # cc19 は "/bin/ld16" と焼き込んでいる。中身は**名前を言う** ld17 を
    # 置く —— 落ちたときに「どれかが足りない」で終わらせない (5.1)
    cp tmp/build/ld17cmd  "$root/bin/ld16"
    cp tmp/build/sh2.bin  "$root/bin/sh2"
    cp tmp/build/sh2.bin  "$root/sh2"
    cp tmp/build/cc19     "$root/cc19"
    cp tmp/build/ar17     "$root/ar"
    cp "$LIBC"/include/*.h     "$root/include/"
    cp "$LIBC"/include/sys/*.h "$root/include/sys/"
    # **/lib は 1 揃いだけ。** cc19 は /lib/*.o を全部並べるので，
    # 多重定義にならない組合せにする (tcc17.sh do_root と同じ)。
    #
    # **ヘッダと .o は同じ世代にする。** 最初 libc21 のヘッダに libc20 の
    # .o を合わせていたので，libc21 で足した実体 (signal / memchr /
    # strerror) が「宣言はあるのに無い」状態になり，結合で落ちた
    # ctype は tcc の作業場では要らなかったので tcc17.sh は置いていない。
    # bzip2 の bzlib.c が isdigit を呼ぶ (ld17 が名前で言った) ので置く。
    # src/morecore は posix/morecore と同じものを別の環境向けに定義する
    # ので，どちらか一方だけ
    for f in l21_src_string l21_src_ctype l21_src_stdlib l21_src_misc15 \
             l21_posix_sys l21_posix_morecore l21_posix_stdio \
             l21_posix_assert l21_posix_dir l21_posix_signal rt64 rtfp; do
        need "tmp/build/$f.o" "sh tools/build.sh stage017"
        cp "tmp/build/$f.o" "$root/lib/"
    done
    for f in docs/external/zlib/*.c docs/external/zlib/*.h; do cp "$f" "$root/z/"; done
    for f in docs/external/bzip2/*.c docs/external/bzip2/*.h; do cp "$f" "$root/bz/"; done
    # 駆動は**我々が書いたもの**である (tests/stage017/ext/)。外部の
    # ソースは素材であって，測るのは我々の器と我々の OS である
    cp tests/stage017/ext/zt.c  "$root/z/"
    cp tests/stage017/ext/bzt.c "$root/bz/"
    # **zlib 自身の検査も入力として読む。** 我々が書いた駆動 (zt.c) は
    # 我々が思いついた道しか通らない。example.c は gzopen / gzprintf /
    # gzseek / gzgets / gzungetc / inflateSync / 辞書つき伸長まで通すので，
    # **libc のファイル層まで一緒に測れる** (docs/stage017-gcc.md 5.1)
    cp docs/external/zlib/test/example.c "$root/z/"
    echo "root: $(find "$root" -type f | wc -l) ファイル" >&2
}

# root を詰めて起動し，出力を $out/$1 に落とす
boot_img() {
    sh tools/sfs3.sh pack "$root" "$out/fs.img" 67108864 1024 > /dev/null
    rm -f "$out/ram"
    dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null
    dd if="$out/fs.img" of="$out/ram" bs=64K oflag=seek_bytes \
        seek=67108864 conv=notrunc 2> /dev/null
    STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-3600} \
        STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
        sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
        > "$out/$1" 2>&1 || true
}

do_probe() {
    do_root
    {
        printf 'echo "---- z ----"\n'
        for u in $Z_UNITS; do
            printf 'cc19 -c z/%s.c -o z/%s.o -I z\necho "z-%s $?"\n' "$u" "$u" "$u"
        done
        printf 'echo "---- bz ----"\n'
        for u in $BZ_UNITS; do
            printf 'cc19 -c bz/%s.c -o bz/%s.o -I bz\necho "bz-%s $?"\n' "$u" "$u" "$u"
        done
        printf 'echo "---- end ----"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    boot_img probe.log
    cat "$out/probe.log"
    # **通らなかった段とその理由を数える。** 通った数ではない
    grep -E '^(z|bz)-[a-z0-9]+ [^0]' "$out/probe.log" > "$out/bad.txt" || true
    echo "---- 通らなかった単位: $(grep -c . "$out/bad.txt") / \
$(( $(echo $Z_UNITS | wc -w) + $(echo $BZ_UNITS | wc -w) )) ----" >&2
    grep -oE "include file '[^']+' not found" "$out/probe.log" | sort -u >&2 || true
    grep -oE "error: [^\\n]*" "$out/probe.log" | sort | uniq -c | sort -rn | head -20 >&2 || true
}

# **書庫にまとめ，駆動を繋いで走らせる。** ここまでやって初めて
# 「我々の器が zlib / bzip2 を組めた」と言える。ar も cc19 も走行も
# 全部 OS の中である —— ホストの ar / ld を使ったら測っているものが
# 変わる (tools/tcc17.sh do_link と同じ理由)
do_run() {
    do_root
    _zo=
    for u in $Z_UNITS; do _zo="$_zo z/$u.o"; done
    _bo=
    for u in $BZ_UNITS; do _bo="$_bo bz/$u.o"; done
    {
        printf 'echo "---- cc ----"\n'
        for u in $Z_UNITS; do
            printf 'cc19 -c z/%s.c -o z/%s.o -I z\necho "z-%s $?"\n' "$u" "$u" "$u"
        done
        for u in $BZ_UNITS; do
            printf 'cc19 -c bz/%s.c -o bz/%s.o -I bz\necho "bz-%s $?"\n' "$u" "$u" "$u"
        done
        printf 'echo "---- ar ----"\n'
        printf 'ar rcs z/libz.a%s\necho "arz $?"\n' "$_zo"
        printf 'ar rcs bz/libbz2.a%s\necho "arbz $?"\n' "$_bo"
        # **出来た書庫を我々自身の ar で読み直す。** ファイルが出たことと
        # 読めることは別である
        printf 'ar t z/libz.a\necho "arzt $?"\n'
        printf 'ar t bz/libbz2.a\necho "arbzt $?"\n'
        printf 'echo "---- link ----"\n'
        # **落ちたら ld の言い分を見る。** ld の標準出力は像なので，
        # 落ちた走行では出力ファイルの中身が診断そのものである (ld17)。
        #
        # `A && echo ok || cat` の形にする。通れば "linkz 0" が出て
        # cat は走らない (像が記録に混ざらない)。落ちれば行が出ずに
        # 診断が出る —— 検査は行の有無で見る
        printf 'cc19 -o zt z/zt.c z/libz.a -I z && echo "linkz 0" || cat zt\n'
        printf 'cc19 -o bzt bz/bzt.c bz/libbz2.a -I bz && echo "linkbz 0" || cat bzt\n'
        printf 'cc19 -o zex z/example.c z/libz.a -I z && echo "linkzex 0" || cat zex\n'
        printf 'echo "---- run ----"\n'
        printf 'zt\necho "runz $?"\n'
        printf 'bzt\necho "runbz $?"\n'
        printf 'echo "---- zex ----"\n'
        printf 'zex\necho "runzex $?"\n'
        printf 'echo "---- end ----"\n'
    } > "$root/go.sh"
    printf 'sh2 go.sh\n' > "$root/boot"
    boot_img run.log
    cat "$out/run.log"
    _rc=0
    for k in arz arbz arzt arbzt linkz linkbz linkzex runz runbz runzex; do
        grep -q "^$k 0\$" "$out/run.log" || { echo "FAIL: $k" >&2; _rc=1; }
    done
    grep -q '^z ok$' "$out/run.log" || { echo "FAIL: zlib の往復" >&2; _rc=1; }
    grep -q '^bz ok ' "$out/run.log" || { echo "FAIL: bzip2 の往復" >&2; _rc=1; }
    # **往復するだけでは足りない。** 我々の誤りが往路と復路で打ち消し
    # 合えば，中身が違っても元に戻る。**外の物差しと値を突き合わせる**。
    #
    # 期待値の出どころは 2 つ (同じ値になることを確かめてある) ——
    #   - ホストの gcc に**同じソースと同じ駆動**を組ませたもの
    #   - Python の zlib (別実装) に同じバイト列を食わせたもの
    #
    # これを入れる前は adler32 が誤っていたのに "z ok" が出ていた
    # (docs/stage017-gcc.md 5.1。cc15u で直した)
    for w in 'crc32 282d245a' 'adler32 459e7153' \
             'level 1 ok 3042' 'level 5 ok 1537' 'level 9 ok 1537' \
             'bz ok 927'; do
        grep -q "^$w\$" "$out/run.log" \
            || { echo "FAIL: 値が合わない ($w)" >&2; _rc=1; }
    done
    # **zlib 自身の検査の出力を，ホストで組んだものと 1 行ずつ突き合わせる。**
    #
    # 版と compile flags の行だけは外す —— flags は uInt / uLong /
    # voidpf / z_off_t の大きさを畳んだ値なので，RV32 (0x55) と
    # x86-64 (0xa9) で必ず違う。**それ以外は 1 文字も違ってはいけない**
    for w in 'uncompress(): hello, hello!' \
             'gzread(): hello, hello!' \
             'gzgets() after gzseek:  hello!' \
             'inflate(): hello, hello!' \
             'large_inflate(): OK' \
             'after inflateSync(): hello, hello!' \
             'inflate with dictionary: hello, hello!'; do
        grep -qF "$w" "$out/run.log" \
            || { echo "FAIL: example の出力が合わない ($w)" >&2; _rc=1; }
    done
    [ "$_rc" -eq 0 ] && echo "---- zlib / bzip2 が我々の器で組めて走った ----" >&2
    return $_rc
}

case ${1:-probe} in
probe) do_probe ;;
run) do_run ;;
clean) rm -rf "$out" ;;
*) echo "usage: ext17.sh [probe|run|clean]" >&2; exit 2 ;;
esac
