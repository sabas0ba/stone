# stage017 のビルド手順と，入力・生成物の宣言。
#
# 第 1 部は**コンパイラをコマンドとして持つ**ことである
# (docs/stage017-cc.md)。作るものは 2 種類ある。
#
#   1. pp16cmd / cc15pcmd / ld16cmd
#      既にある .o を 'E' で組み直しただけのもの。ソースは無い。
#      tmp/build の pp16.bin / cc15p.bin / ld16.bin は**平らな像**で
#      あり，QEMU に -bios で直に置いて走らせる形をしている。OS の
#      上の実行形式にするには ELF (前置部 'E') でリンクし直す必要が
#      ある。Stage 13 の pp13cmd / cc13cmd / ld13cmd と同じ手である
#      (docs/stage013-tools.md 7 章)。
#
#   2. cc17
#      駆動役。上の 3 つを順に呼ぶ。libc の第 18 世代とリンクする。

# 既にある .o を OS の実行形式へ組み直す。cmdlink <名前> <元の .o の名前>
cmdlink() {
    step "$1" "$1" \
        -- "tmp/build/${2}.o" tmp/build/ld16.bin \
        -- cmdlink_run "$1" "$2"
}

cmdlink_run() {
    { printf 'E'; cat "tmp/build/${2}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
}

build_stage017() {
    cmdlink pp16cmd pp16
    cmdlink cc15pcmd cc15p
    # tcc の作業場で使う器。cc15p は静的な初期化子の文字列を壊すので
    # (docs/stage017-cc.md 24〜28 章)，tcc を正しく組むには cc15q が要る。
    # 鎖のプログラムは今までどおり cc15pcmd で組む
    cmdlink cc15qcmd cc15q
    # 第 18 世代。文字列リテラルの sizeof を直したもの。tcc が書庫を
    # 読むのにこれが要る (docs/stage017-cc.md 31 章)
    cmdlink cc15rcmd cc15r
    # 第 19 世代。多次元の char 配列の初期化子を直したもの (33 章)。
    # tcc はこの形を使わないので tcc のバイト列は変わらないが、
    # **最前線を 2 つに分けない** —— 台帳を測る器と tcc を組む器は
    # 同じものにする
    cmdlink cc15scmd cc15s
    # 第 20 世代。宣言指定子の途中に来る型修飾子 (unsigned const char)。
    # zlib を我々の器で訳して出た穴で，これが最前線である
    # (docs/stage017-gcc.md 5.1)
    cmdlink cc15tcmd cc15t
    # 第 21 世代。複合代入の符号 (5.1)。これが最前線である
    cmdlink cc15ucmd cc15u
    # 第 22 世代。スカラの初期化子の溢れ (5.2)
    cmdlink cc15vcmd cc15v
    # 第 28 世代。GCC 4.7.4 の 70 単位を測って出た 6 つを埋めた先
    # (docs/stage017-gcc.md 8.3)。**これが最前線である。**
    # 検査の第 7 部が OS の上で使う
    cmdlink cc15abcmd cc15ab
    cmdlink ld16cmd ld16
    # 未定義シンボルの名前を言うリンカ (5.1)。cc19 は器の位置を
    # "/bin/ld16" と焼き込んでいるので、置くときは名前を ld16 にする
    cmdlink ld17cmd ld17

    step cc17 cc17 \
        -- stage017/cc17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- cc17_run

    # 第 2 部。cc17 は記録対象なので書き換えず世代を刻む
    # (roadmap.md 4 章)。ar17 は新しい道具なので第 1 世代
    step cc18 cc18 \
        -- stage017/cc18.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run cc18 stage017/cc18.c

    step ar17 ar17 \
        -- stage017/ar17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run ar17 stage017/ar17.c

    # make の第 17 世代 (第 3 部の 1)
    step mk17 mk17 \
        -- stage017/mk17.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l18_posix_dir.o \
        -- osprog_run mk17 stage017/mk17.c

    # カーネルの第 23 世代。kernel22 との差は引数の数と長さだけ
    # (docs/stage017-cc.md 8 章)。前置部は 'K' である
    # libc の第 19 世代 (第 4 部の 1)。libc18 との差は stat だけ。
    # 翻訳は libc18 と同じ cc15k で行う (器の都合。stage016.sh と揃える)
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir; do
        n=$(echo "$f" | tr / _)
        step "l19_$n" "l19_$n.o" \
            -- "stage017/libc19/$f.c" \
               stage017/libc19/include/*.h \
               stage017/libc19/include/sys/time.h \
               stage017/libc19/include/sys/stat.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc19_run "$f" "$n"
    done

    # make の第 18 世代 (第 4 部の 2)。**libc19 と繋ぐ** (stat が要る)
    step mk18 mk18 \
        -- stage017/mk18.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk18 stage017/mk18.c

    # make の第 19 世代 (第 3 部の 2)。関数を持つ
    step mk19 mk19 \
        -- stage017/mk19.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk19 stage017/mk19.c

    # libc の第 20 世代 (第 3 部の 3 の 3)。libc19 との差は
    # 助言的ロック (fcntl) と getpid と EINTR だけ。tcc の lib/tcov.c が
    # 要る (docs/stage017-cc.md 27 章)
    #
    # **ヘッダも入力に数える。** libc*_run は include/*.h を束ねてから
    # 翻訳するので，ヘッダを直したら .o が変わりうる。ここに書かないと
    # 外側の stage の印だけが変わって build_stage017 が走り，中の step は
    # 「前と同じ」と言って**古い .o を持ち回ったまま新しい印が書かれる**。
    # 数えるのは束ねているものと同じ並びにすること
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir; do
        n=$(echo "$f" | tr / _)
        step "l20_$n" "l20_$n.o" \
            -- "stage017/libc20/$f.c" \
               stage017/libc20/include/*.h \
               stage017/libc20/include/sys/time.h \
               stage017/libc20/include/sys/stat.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc20_run "$f" "$n"
    done

    # libc の第 21 世代 (5.1)。libc20 との差は，実物 (zlib / bzip2) の
    # ソースを読んで判った穴 —— sys/types.h・signal.h・memchr・strerror・
    # lseek の宣言，そして **fopen(path, "a") が末尾から書くこと**
    # (Stage 14 から残っていた誤り)。
    #
    # **ここまで .o にしていなかった。** ヘッダだけ足して満足していたので，
    # 新しく実装したもの (signal / memchr / strerror) はどこにも無かった。
    # 5.1 の結合で「宣言はあるのに実体が無い」で落ちて判った
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir \
             posix/signal; do
        n=$(echo "$f" | tr / _)
        step "l21_$n" "l21_$n.o" \
            -- "stage017/libc21/$f.c" \
               stage017/libc21/include/*.h \
               stage017/libc21/include/sys/time.h \
               stage017/libc21/include/sys/stat.h \
               stage017/libc21/include/sys/types.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc21_run "$f" "$n"
    done

    # libc の第 22 世代 (8.3 の 8)。libc21 との差は stdio.h の 1 か所 ——
    # C89 7.9.7.8 の putc を**マクロとして**置いた。関数として宣言しても
    # 鎖の前置部の 1 引数 putc に負けるので，pp の段で fputc へ書き換える。
    # GCC の libcpp 3 単位 (lex / line-map / mkdeps) がこれで .o まで通る
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir \
             posix/signal; do
        n=$(echo "$f" | tr / _)
        step "l22_$n" "l22_$n.o" \
            -- "stage017/libc22/$f.c" \
               stage017/libc22/include/*.h \
               stage017/libc22/include/sys/time.h \
               stage017/libc22/include/sys/stat.h \
               stage017/libc22/include/sys/types.h \
               tmp/build/cc15k.bin tmp/build/pp.bin \
            -- libc22_run "$f" "$n"
    done

    # libc の第 23 世代 (docs/stage017-gcc.md 5.3 / 5.4 / 6.3)。libc22 との
    # 差は posix/stdio.c と src/stdlib.c の 2 本で、**差分試験の OS 側
    # (tools/diff17.sh os) がホストと突き合わせて見つけた食い違い**である
    # —— printf の旗・精度・%o / %X、strtoul の空白と endptr、
    # fgets(b, 1, f)、そして %g / %e / %E / %G / %F の浮動小数点変換。
    #
    # **この世代から cc15k では組めない。** l19〜l22 は cc15k で組んで
    # いたが、libc23 の posix/stdio.c は 5 で拒まれる。最前線の cc15ab で
    # 組む (stage017/libc23.md)
    for f in src/string src/ctype src/stdlib src/morecore src/misc15 \
             posix/sys posix/morecore posix/stdio posix/assert posix/dir \
             posix/signal; do
        n=$(echo "$f" | tr / _)
        step "l23_$n" "l23_$n.o" \
            -- "stage017/libc23/$f.c" \
               stage017/libc23/include/*.h \
               stage017/libc23/include/sys/time.h \
               stage017/libc23/include/sys/stat.h \
               stage017/libc23/include/sys/types.h \
               tmp/build/cc15ab.bin tmp/build/pp.bin \
            -- libc23_run "$f" "$n"
    done

    # ---- configure が使う道具 (docs/stage017-gcc.md 5.5〜5.9) ----
    #
    # GCC の configure は sed と awk が無ければ 1 行も進まない。
    # **世代はすべて main の最前線 (cc15ab) と libc23 で組む** ——
    # 元は別の枝で刻まれた世代で、あちらの Stage 18 の器で組まれていたが、
    # 道具の側はその器を要求していない (どれも C89 の内側で書かれている)

    # sed の第 1 世代 (5.5)。正規表現機構を自前で持つ
    step sed1 sed1 \
        -- stage017/sed1.c tmp/build/cc15ab.bin tmp/build/pp16.bin \
           tmp/build/ld17.bin tmp/build/l23_posix_dir.o \
        -- osprog23_run sed1 stage017/sed1.c

    # 正規表現機構 (5.6)。sed2 と sh3 が分け合う。**写しを 2 つ持たない**
    step re1 re1.o \
        -- stage017/re1.c stage017/re1.h tmp/build/cc15ab.bin \
           tmp/build/pp16.bin \
        -- osobj23_run re1 stage017/re1.c

    # sed の第 2 世代 (5.6)。機構を re1 へ移しただけで、受ける形は同じ
    step sed2 sed2 \
        -- stage017/sed2.c stage017/re1.h tmp/build/re1.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run sed2 stage017/sed2.c tmp/build/re1.o

    # シェルの第 3 世代 (5.6)。grep を正規表現へ引き上げ、configure が
    # 使う道具 (tr / expr / basename / dirname / wc / sort / touch /
    # chmod) を足したもの
    step sh3 sh3 \
        -- stage017/sh3.c stage017/re1.h tmp/build/re1.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run sh3 stage017/sh3.c tmp/build/re1.o

    # 正規表現機構の第 2 世代 (5.7)。ERE・選択・{n,m}・字種を受け、
    # 組の中へ後戻りできる。awk はこれが無いと書けない
    step re2 re2.o \
        -- stage017/re2.c stage017/re2.h tmp/build/cc15ab.bin \
           tmp/build/pp16.bin \
        -- osobj23_run re2 stage017/re2.c

    # sed の第 3 世代 (5.7)。機構を re2 へ替えたもの
    step sed3 sed3 \
        -- stage017/sed3.c stage017/re2.h tmp/build/re2.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run sed3 stage017/sed3.c tmp/build/re2.o

    # シェルの第 4 世代 (5.7)。grep -E / egrep が使えるようになった
    step sh4 sh4 \
        -- stage017/sh4.c stage017/re2.h tmp/build/re2.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run sh4 stage017/sh4.c tmp/build/re2.o

    # シェルの第 5 世代 (5.9)。経路展開 (glob) を持つ
    step sh5 sh5 \
        -- stage017/sh5.c stage017/re2.h tmp/build/re2.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run sh5 stage017/sh5.c tmp/build/re2.o

    # awk の数と書式 (5.8)。**翻訳単位を分けてある** —— awk はこの鎖で
    # いちばん大きなプログラムで、1 ファイルでは我々の cc の表が溢れる
    step awkfmt1 awkfmt1.o \
        -- stage017/awkfmt1.c stage017/awkfmt1.h tmp/build/cc15ab.bin \
           tmp/build/pp16.bin \
        -- osobj23_run awkfmt1 stage017/awkfmt1.c

    # awk の第 1 世代 (5.8)。configure が使う道具の最後の 1 つで、
    # re2 の ERE を使う
    step awk1 awk1 \
        -- stage017/awk1.c stage017/re2.h stage017/awkfmt1.h \
           tmp/build/re2.o tmp/build/awkfmt1.o \
           tmp/build/cc15ab.bin tmp/build/pp16.bin tmp/build/ld17.bin \
        -- osprog23_run awk1 stage017/awk1.c tmp/build/re2.o tmp/build/awkfmt1.o

    # 前処理器の第 17 世代 (第 3 部の 3 の 2)。-I を探す道として持つ。
    # **libc を繋がない** —— sys_* は 'E' 前置部のものを直に呼ぶ
    # (docs/stage017-cc.md 17 章)
    step pp17 pp17 \
        -- stage017/pp17.sc tmp/build/cc15p.bin tmp/build/ld16.bin \
        -- pp17_run

    # 前処理器の第 18 世代。**展開の結果に現れた defined を評価する**
    # (docs/stage017-gcc.md 8.4 / stage017/pp18.md)。pp17 の全文複製で，
    # 差は #if の式を読むところだけである
    step pp18 pp18 \
        -- stage017/pp18.sc tmp/build/cc15p.bin tmp/build/ld16.bin \
        -- pp18_run

    # cc の第 19 世代 (第 3 部の 3 の 2)。-I を束ねず pp17 へ渡す
    step cc19 cc19 \
        -- stage017/cc19.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run cc19 stage017/cc19.c

    # make の第 20 世代 (第 3 部の 3 の 1)。tcc の Makefile を読む
    step mk20 mk20 \
        -- stage017/mk20.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run mk20 stage017/mk20.c

    # 時刻を読む検査用のプログラム (第 4 部の 1)。**libc19 と繋ぐ**
    step stamp stamp \
        -- tests/stage017/user/stamp.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin tmp/build/l19_posix_dir.o \
        -- osprog19_run stamp tests/stage017/user/stamp.c

    step kernel23 kernel23.bin \
        -- stage017/kernel23.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kernel23_run

    # カーネルの第 24 世代。kernel23 との差は sfs3 と時刻だけ
    # (docs/stage017-cc.md 11 章)
    step kernel24 kernel24.bin \
        -- stage017/kernel24.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kern17 kernel24 stage017/kernel24.c

    # カーネルの第 25 世代。kernel24 との差は sfs4 と配置だけ
    # (docs/stage017-gcc.md 7 章)
    step kernel25 kernel25.bin \
        -- stage017/kernel25.c tmp/build/cc15p.bin tmp/build/pp16.bin \
           tmp/build/ld16.bin \
        -- kern17 kernel25 stage017/kernel25.c
}

# カーネルを 1 つ作る (前置部は 'K')。stage016.sh の kern と同じ手だが，
# **この階層のことはこの階層で書く** (読む順に依らせない)
kern17() {
    sh tools/bundle.sh "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'K'; cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

libc21_run() {
    sh tools/bundle.sh stage017/libc21/include/*.h \
        "sys/time.h=stage017/libc21/include/sys/time.h" \
        "sys/stat.h=stage017/libc21/include/sys/stat.h" \
        "sys/types.h=stage017/libc21/include/sys/types.h" \
        "stage017/libc21/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l21_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l21_$2.i" \
        > "tmp/build/l21_$2.o"
    echo "built tmp/build/l21_$2.o" >&2
}

libc23_run() {
    sh tools/bundle.sh stage017/libc23/include/*.h \
        "sys/time.h=stage017/libc23/include/sys/time.h" \
        "sys/stat.h=stage017/libc23/include/sys/stat.h" \
        "sys/types.h=stage017/libc23/include/sys/types.h" \
        "stage017/libc23/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l23_$2.i"
    sh tools/env.sh qemu tmp/build/cc15ab.bin < "tmp/build/l23_$2.i" \
        > "tmp/build/l23_$2.o"
    echo "built tmp/build/l23_$2.o" >&2
}

# libc23 と最前線の器 (cc15ab) で組む OS プログラム。
# **新しい道具はここから作る** —— 凍結した世代 (libc19 / cc15p) は
# 既存の成果物のためのもので、新しく書くものを縛る理由が無い
osprog23_run() {
    _nm=$1
    _src=$2
    shift 2
    sh tools/bundle.sh stage017/libc23/include/*.h \
        "sys/time.h=stage017/libc23/include/sys/time.h" \
        "sys/stat.h=stage017/libc23/include/sys/stat.h" \
        "sys/types.h=stage017/libc23/include/sys/types.h" \
        stage017/re1.h stage017/re2.h stage017/awkfmt1.h "$_src" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${_nm}.i"
    sh tools/env.sh qemu tmp/build/cc15ab.bin < "tmp/build/${_nm}.i" \
        > "tmp/build/${_nm}.o"
    # shellcheck disable=SC2086
    { printf 'E'; cat "tmp/build/${_nm}.o" $* \
        tmp/build/l23_src_string.o tmp/build/l23_src_ctype.o \
        tmp/build/l23_src_stdlib.o tmp/build/l23_src_misc15.o \
        tmp/build/l23_posix_sys.o tmp/build/l23_posix_morecore.o \
        tmp/build/l23_posix_stdio.o tmp/build/l23_posix_assert.o \
        tmp/build/l23_posix_dir.o tmp/build/l23_posix_signal.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld17.bin > "tmp/build/$_nm"
    echo "built tmp/build/$_nm" >&2
}

# 道具どうしで分け合う部品を 1 つ .o にする (re1 / re2 / awkfmt1)
osobj23_run() {
    sh tools/bundle.sh stage017/libc23/include/*.h \
        "sys/time.h=stage017/libc23/include/sys/time.h" \
        "sys/stat.h=stage017/libc23/include/sys/stat.h" \
        "sys/types.h=stage017/libc23/include/sys/types.h" \
        stage017/re1.h stage017/re2.h stage017/awkfmt1.h "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15ab.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    echo "built tmp/build/${1}.o" >&2
}

libc22_run() {
    sh tools/bundle.sh stage017/libc22/include/*.h \
        "sys/time.h=stage017/libc22/include/sys/time.h" \
        "sys/stat.h=stage017/libc22/include/sys/stat.h" \
        "sys/types.h=stage017/libc22/include/sys/types.h" \
        "stage017/libc22/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l22_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l22_$2.i" \
        > "tmp/build/l22_$2.o"
    echo "built tmp/build/l22_$2.o" >&2
}

libc20_run() {
    sh tools/bundle.sh stage017/libc20/include/*.h \
        "sys/time.h=stage017/libc20/include/sys/time.h" \
        "sys/stat.h=stage017/libc20/include/sys/stat.h" \
        "stage017/libc20/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l20_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l20_$2.i" \
        > "tmp/build/l20_$2.o"
    echo "built tmp/build/l20_$2.o" >&2
}

libc19_run() {
    sh tools/bundle.sh stage017/libc19/include/*.h \
        "sys/time.h=stage017/libc19/include/sys/time.h" \
        "sys/stat.h=stage017/libc19/include/sys/stat.h" \
        "stage017/libc19/$1.c" \
        | sh tools/env.sh qemu tmp/build/pp.bin > "tmp/build/l19_$2.i"
    sh tools/env.sh qemu tmp/build/cc15k.bin < "tmp/build/l19_$2.i" \
        > "tmp/build/l19_$2.o"
    echo "built tmp/build/l19_$2.o" >&2
}

# OS の上で動く実行形式を 1 つ作る。**libc19 と繋ぐ** 版
osprog19_run() {
    sh tools/bundle.sh stage017/libc19/include/*.h \
        "sys/stat.h=stage017/libc19/include/sys/stat.h" \
        "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'E'; cat "tmp/build/${1}.o" \
        tmp/build/l19_src_string.o tmp/build/l19_src_stdlib.o \
        tmp/build/l19_src_misc15.o tmp/build/l19_posix_sys.o \
        tmp/build/l19_posix_morecore.o tmp/build/l19_posix_stdio.o \
        tmp/build/l19_posix_assert.o tmp/build/l19_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
}

# pp17 は .sc なので前処理を通さない (tool1 と同じ道)。ただし 'E' で
# 組む —— sys_openat などのスタブは 'E' 前置部にしかないからである。
# フラットで組むと未定義で落ちる (実測 rc=7)
pp17_run() {
    { cat stage017/pp17.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15p.bin > tmp/build/pp17.o
    { printf 'E'; cat tmp/build/pp17.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/pp17
    echo "built tmp/build/pp17" >&2
}

pp18_run() {
    { cat stage017/pp18.sc; printf '\004'; } \
        | sh tools/env.sh qemu tmp/build/cc15p.bin > tmp/build/pp18.o
    { printf 'E'; cat tmp/build/pp18.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/pp18
    echo "built tmp/build/pp18" >&2
}

kernel23_run() {
    sh tools/bundle.sh stage017/kernel23.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/kernel23.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/kernel23.i \
        > tmp/build/kernel23.o
    { printf 'K'; cat tmp/build/kernel23.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/kernel23.bin
    echo "built tmp/build/kernel23.bin" >&2
}

# OS の上で動く実行形式を 1 つ作る。osprog_run <名前> <ソース>
#
# **並べる .o は sh2 / cc17 と同じ 1 揃いである。** ここと
# tests/stage017 の /lib が食い違うと，駆動役が組んだものだけ
# 挙動が変わる (docs/stage017-cc.md 3.2)
osprog_run() {
    sh tools/bundle.sh stage016/libc18/include/*.h \
        "sys/stat.h=stage016/libc18/include/sys/stat.h" \
        "$2" \
        | sh tools/env.sh qemu tmp/build/pp16.bin > "tmp/build/${1}.i"
    sh tools/env.sh qemu tmp/build/cc15p.bin < "tmp/build/${1}.i" \
        > "tmp/build/${1}.o"
    { printf 'E'; cat "tmp/build/${1}.o" \
        tmp/build/l18_src_string.o tmp/build/l18_src_stdlib.o \
        tmp/build/l18_src_misc15.o tmp/build/l18_posix_sys.o \
        tmp/build/l18_posix_morecore.o tmp/build/l18_posix_stdio.o \
        tmp/build/l18_posix_assert.o tmp/build/l18_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > "tmp/build/$1"
    echo "built tmp/build/$1" >&2
}

cc17_run() {
    sh tools/bundle.sh stage016/libc18/include/*.h \
        "sys/stat.h=stage016/libc18/include/sys/stat.h" \
        stage017/cc17.c \
        | sh tools/env.sh qemu tmp/build/pp16.bin > tmp/build/cc17.i
    sh tools/env.sh qemu tmp/build/cc15p.bin < tmp/build/cc17.i \
        > tmp/build/cc17.o
    # **/lib へ置く 1 揃いと同じ並びである。** ここと tests/stage017 の
    # /lib が食い違うと，駆動役が組んだものだけ挙動が変わる
    { printf 'E'; cat tmp/build/cc17.o \
        tmp/build/l18_src_string.o tmp/build/l18_src_stdlib.o \
        tmp/build/l18_src_misc15.o tmp/build/l18_posix_sys.o \
        tmp/build/l18_posix_morecore.o tmp/build/l18_posix_stdio.o \
        tmp/build/l18_posix_assert.o tmp/build/l18_posix_dir.o \
        tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
        | sh tools/env.sh qemu tmp/build/ld16.bin > tmp/build/cc17
    echo "built tmp/build/cc17" >&2
}

do_stage017() {
    run_stage stage017 pp16cmd cc15pcmd cc15qcmd cc15rcmd cc15scmd cc15tcmd cc15ucmd cc15vcmd cc15abcmd ld16cmd ld17cmd cc17 cc18 cc19 ar17 pp17 pp18 mk17 mk18 mk19 mk20 stamp \
        kernel23.bin kernel24.bin kernel25.bin \
        l19_src_string.o l19_src_ctype.o l19_src_stdlib.o \
        l19_src_morecore.o l19_src_misc15.o \
        l19_posix_sys.o l19_posix_morecore.o l19_posix_stdio.o \
        l19_posix_assert.o l19_posix_dir.o \
        l20_src_string.o l20_src_ctype.o l20_src_stdlib.o \
        l20_src_morecore.o l20_src_misc15.o \
        l20_posix_sys.o l20_posix_morecore.o l20_posix_stdio.o \
        l20_posix_assert.o l20_posix_dir.o \
        l21_src_string.o l21_src_ctype.o l21_src_stdlib.o \
        l21_src_morecore.o l21_src_misc15.o \
        l21_posix_sys.o l21_posix_morecore.o l21_posix_stdio.o \
        l21_posix_assert.o l21_posix_dir.o l21_posix_signal.o \
        l22_src_string.o l22_src_ctype.o l22_src_stdlib.o \
        l22_src_morecore.o l22_src_misc15.o \
        l22_posix_sys.o l22_posix_morecore.o l22_posix_stdio.o \
        l22_posix_assert.o l22_posix_dir.o l22_posix_signal.o \
        l23_src_string.o l23_src_ctype.o l23_src_stdlib.o \
        l23_src_morecore.o l23_src_misc15.o \
        l23_posix_sys.o l23_posix_morecore.o l23_posix_stdio.o \
        l23_posix_assert.o l23_posix_dir.o l23_posix_signal.o \
        sed1 sed2 sed3 re1.o re2.o sh3 sh4 sh5 awkfmt1.o awk1 \
        -- stage017/cc17.c stage017/cc18.c stage017/cc19.c stage017/ar17.c \
           stage017/pp17.sc stage017/pp18.sc \
           stage017/mk17.c stage017/mk18.c stage017/mk19.c \
           stage017/mk20.c \
           stage017/kernel23.c stage017/kernel24.c stage017/kernel25.c \
           tests/stage017/user/stamp.c \
           stage017/libc19/include/*.h stage017/libc19/include/sys/*.h \
           stage017/libc19/src/*.c stage017/libc19/posix/*.c \
           stage017/libc20/include/*.h stage017/libc20/include/sys/*.h \
           stage017/libc20/src/*.c stage017/libc20/posix/*.c \
           stage017/libc21/include/*.h stage017/libc21/include/sys/*.h \
           stage017/libc21/src/*.c stage017/libc21/posix/*.c \
           stage017/libc22/include/*.h stage017/libc22/include/sys/*.h \
           stage017/libc22/src/*.c stage017/libc22/posix/*.c \
           stage017/libc23/include/*.h stage017/libc23/include/sys/*.h \
           stage017/libc23/src/*.c stage017/libc23/posix/*.c \
           stage017/sed1.c stage017/sed2.c stage017/sed3.c \
           stage017/re1.c stage017/re1.h stage017/re2.c stage017/re2.h \
           stage017/sh3.c stage017/sh4.c stage017/sh5.c \
           stage017/awk1.c stage017/awkfmt1.c stage017/awkfmt1.h \
           stage016/libc18/include/*.h \
           stage016/libc18/include/sys/*.h \
           tmp/build/stage016.stamp tools/build/stage017.sh tools/bundle.sh
}
