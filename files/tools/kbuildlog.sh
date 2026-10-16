#!/bin/sh
# Linux 5.4 の Kbuild が呼ぶ命令を記録する (docs/stage019-linux.md 5 章)。
#
#   kbuildlog.sh run <出力> <設定の目標> [make の変数=値...]
#       O=<出力> に設定を作ってから vmlinux まで回し，呼ばれた命令を
#       <出力>/kblog/ に残す
#
# stone の OS の中でカーネルを組むには，Kbuild が呼ぶ命令 (コンパイラ・
# アセンブラ・リンカ・シェルの台本・補助プログラム・外部の道具) をすべて
# stone 製で揃える必要がある。その一覧を実測するための道具である。
#
# 本物の RISC-V のツールチェインは使わない。代わりに，呼ばれた引数を記録して
# 空の出力ファイルを作るだけの台本 (stub) を置く。
#
#   cc       GCC 4.6.4 と名乗る (Linux 5.4 が受ける最低の版。新しい版を名乗る
#            ほど Linux は新しい拡張を使う)。-E の標準入力は __GNUC__ などだけを
#            置き換え，-c / -S / -E -o は空のファイルを作り，-MD の依存ファイル
#            (-Wp,-MD,<file>) には「出力: 入力」を書く。cc-option の試験は
#            すべて通るので，実際の cc が受けない選択肢も一覧に出る
#   ld / ar / nm / objcopy / objdump / readelf / size / strip
#            出力ファイルを空で作る (.o は RV32 の ELF の頭 52 バイトだけを持つ)
#   シェル   make の SHELL を記録用の台本に替え，レシピと $(shell ...) を
#            記録してから /bin/sh で実行する
#
# 生成物の中身が空なので，asm-offsets.h などは定義を持たず，vmlinux も
# 空である。ここで得るのは「何が何を引数に呼ばれるか」だけである。
# 設定は tools/kconfref.sh の上流の conf で作る (flex が要る)。
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
linux="$repo_root/docs/external/linux54"
flexbin="$repo_root/tmp/kconfref/flex/src/flex"

die() { echo "kbuildlog.sh: $*" >&2; exit 1; }

# stub を <dir>/bin に作る。記録先は環境変数 KBLOG
make_stubs() {
    bin=$1
    mkdir -p "$bin"
    cat > "$bin/stub" <<'EOF'
#!/bin/sh
# Kbuild の測定用の stub (tools/kbuildlog.sh)。引数を記録し，出力を空で作る
tool=${0##*/}
tool=${tool#riscv32-stub-}
{
    printf '%s' "$tool"
    for a in "$@"; do printf ' %s' "$a"; done
    printf '\n'
} >> "$KBLOG/tools.log"
out=
dep=
mode=
src=
prev=
for a in "$@"; do
    case "$prev" in
    -o) out=$a ;;
    -MF) dep=$a ;;
    esac
    case "$a" in
    -Wp,-MD,*) dep=${a#-Wp,-MD,} ;;
    -Wp,-MMD,*) dep=${a#-Wp,-MMD,} ;;
    -E|-c|-S) mode=$a ;;
    -o*) [ "$a" != -o ] && out=${a#-o} ;;
    -*) ;;
    *) src=$a ;;
    esac
    prev=$a
done
case "$tool" in
gcc)
    case " $* " in
    *" --version "*) echo "riscv32-stub-gcc (GCC) 4.6.4"; exit 0 ;;
    *" -v "*) echo "gcc version 4.6.4 (stub)" >&2; exit 0 ;;
    *" -dumpversion "*) echo "4.6.4"; exit 0 ;;
    *" -print-file-name=include "*) echo "$KBLOG/include"; exit 0 ;;
    *" -print-libgcc-file-name "*) echo "$KBLOG/libgcc.a"; exit 0 ;;
    esac
    if [ "$mode" = -E ] && [ "$src" = - ] && [ -z "$out" ]; then
        sed -e 's/__GNUC__/4/g' -e 's/__GNUC_MINOR__/6/g' -e 's/__GNUC_PATCHLEVEL__/4/g'
        exit 0
    fi
    ;;
ld)
    case " $* " in
    *" -v "*|*" --version "*) echo "GNU ld (stub) 2.32"; exit 0 ;;
    esac
    ;;
ar)
    # ar の第 2 引数が書庫
    [ $# -ge 2 ] && out=$2
    ;;
nm|objdump|readelf|size)
    exit 0
    ;;
esac
if [ -n "$out" ] && [ "$out" != /dev/null ]; then
    mkdir -p "$(dirname "$out")"
    : > "$out"
    # .o には RV32 の ELF の頭 (52 バイト) だけを置く。mk_elfconfig は
    # 頭の class と byte order を読む
    case "$tool:$mode:$out" in
    gcc:-c:*|ld:*:*.o)
        printf '\177ELF\001\001\001\000\000\000\000\000\000\000\000\000' > "$out"
        printf '\001\000\363\000\001\000\000\000' >> "$out"
        printf '\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' >> "$out"
        printf '\064\000\000\000\050\000\000\000\000\000' >> "$out"
        ;;
    # -S は asm-offsets.c などから定数の見出しを作る段で使われる。
    # DEFINE / OFFSET / DEVID / DEVID_FIELD の名前だけを拾い，値 0 で
    # Kbuild が sed で抜き出す印 ("->名前 $0 0") を出す
    gcc:-S:*)
        sed -n \
            -e 's/^[[:space:]]*DEVID(\([A-Za-z0-9_]*\));.*/	.ascii "->SIZE_\1 $0 0"/p' \
            -e 's/^[[:space:]]*DEVID_FIELD(\([A-Za-z0-9_]*\), *\([A-Za-z0-9_]*\));.*/	.ascii "->OFF_\1_\2 $0 0"/p' \
            -e 's/^[[:space:]]*DEFINE(\([A-Za-z0-9_]*\),.*/	.ascii "->\1 $0 0"/p' \
            -e 's/^[[:space:]]*OFFSET(\([A-Za-z0-9_]*\),.*/	.ascii "->\1 $0 0"/p' \
            "$src" > "$out"
        ;;
    esac
fi
if [ -n "$dep" ]; then
    mkdir -p "$(dirname "$dep")"
    printf '%s: %s\n' "${out:-x.o}" "${src:-/dev/null}" > "$dep"
fi
exit 0
EOF
    chmod +x "$bin/stub"
    for t in gcc ld ar nm objcopy objdump readelf size strip elfedit; do
        ln -sf stub "$bin/riscv32-stub-$t"
    done
    cat > "$bin/logsh" <<'EOF'
#!/bin/sh
# make の SHELL の代わり。命令を記録してから /bin/sh で実行する
printf '%s\n' "$*" >> "$KBLOG/shell.log"
exec /bin/sh "$@"
EOF
    chmod +x "$bin/logsh"
}

run_build() {
    out=$1; target=$2; shift 2
    [ -x "$flexbin" ] || die "flex が無い (sh tools/kconfref.sh flex)"
    # 前回の生成物が残ると作り直されず記録に出ないので，毎回空から始める
    rm -rf "$out"
    mkdir -p "$out"
    out=$(CDPATH='' cd -- "$out" && pwd)
    KBLOG="$out/kblog"
    export KBLOG
    rm -rf "$KBLOG"
    mkdir -p "$KBLOG/include"
    make_stubs "$KBLOG/bin"
    set -- O="$out" ARCH=riscv CROSS_COMPILE="$KBLOG/bin/riscv32-stub-" \
        LEX="$flexbin" YACC=bison SHELL="$KBLOG/bin/logsh" "$@"
    make -C "$linux" "$@" "$target" > "$KBLOG/config.out" 2>&1 \
        || die "設定の目標が失敗した ($KBLOG/config.out)"
    make -k -C "$linux" "$@" V=1 vmlinux > "$KBLOG/build.out" 2>&1 \
        || echo "kbuildlog.sh: vmlinux の目標が失敗した ($KBLOG/build.out)。途中までの記録は残る" >&2
    echo "記録: $KBLOG/tools.log ($(wc -l < "$KBLOG/tools.log" | tr -d ' ') 行)，$KBLOG/shell.log ($(wc -l < "$KBLOG/shell.log" | tr -d ' ') 行)"
}

cmd=${1:-}
case "$cmd" in
run)
    [ $# -ge 3 ] || die "run <出力> <設定の目標> [変数=値...]"
    shift
    run_build "$@"
    ;;
*)
    echo "usage: kbuildlog.sh run <出力> <設定の目標> [変数=値...]" >&2
    exit 2
    ;;
esac
