#!/bin/bash
# Stage 17 の検査。設計は docs/stage017-cc.md。
#
# 第 1 部は**コンパイラをコマンドとして持つ**ことである。我々の OS の
# 上で `cc -o out in.c` と書けば動くものを作った。見るのは
#
#   1. 駆動役が pp16 -> cc15p -> ld16 を通して実行形式を出すこと
#   2. 出た実行形式が我々の OS の上で正しく走ること
#   3. tcc の configure がコンパイラとしてそれを見つけ，検出に成功すること
#
# 3 は素材 (docs/external/tcc) が要るので，無ければ飛ばす。
# **飛ばした回に「通った」と読めてはいけない**ので，飛ばしたことを
# はっきり出す (Stage 16 の configure の節と同じ扱い)。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
out=tmp/s17t
rm -rf "$out"
mkdir -p "$out"

# 我々の OS の根を組む。**/lib に置くのは 1 揃いだけである。**
# 駆動役は /lib の .o を全部並べるので，同じものの別実装 (src/morecore と
# posix/morecore) が両方あると ld が多重定義で落ちる。並びは
# tools/build/stage017.sh の cc17_run と同じでなければならない
mkroot() {
    _r=$1
    rm -rf "$_r"
    mkdir -p "$_r/bin" "$_r/include/sys" "$_r/lib"
    cp tmp/build/pp16cmd "$_r/bin/pp16"
    cp tmp/build/cc15pcmd "$_r/bin/cc15p"
    cp tmp/build/ld16cmd "$_r/bin/ld16"
    cp stage016/libc18/include/*.h "$_r/include/"
    cp stage016/libc18/include/sys/*.h "$_r/include/sys/"
    for _o in l18_src_string l18_src_stdlib l18_src_misc15 l18_posix_sys \
              l18_posix_morecore l18_posix_stdio l18_posix_assert \
              l18_posix_dir rt64 rtfp; do
        cp "tmp/build/$_o.o" "$_r/lib/"
    done
    cp tmp/build/cc17 "$_r/cc"
    cp tmp/build/cc18 "$_r/cc18"
    cp tmp/build/ar17 "$_r/ar"
    cp tmp/build/mk17 "$_r/mk"
    cp tmp/build/sh2.bin "$_r/bin/sh2"
    cp tmp/build/sh2.bin "$_r/sh2"
}

# 根を像にして kernel23 で走らせる。出力は $2 へ。
# 像の大きさは $3 (既定 16 MB) / 項目数は $4 (既定 256)
runroot() {
    sh tools/sfs2.sh pack "$1" "$out/img" "${3:-16777216}" "${4:-256}" \
            > /dev/null \
        && rm -f "$out/ram" \
        && dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/img" of="$out/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel23.bin < /dev/null \
            > "$2" 2>&1
}

# 走ったあとの像を $out/ram から切り出して $2 へ展開する。
# **走らせる前に詰めた $out/img は走ったあとの姿ではない。**
# カーネルが書き換えるのは記憶の 67108864 から先である
unpackback() {
    dd if="$out/ram" of="$out/img.after" bs=64K \
        iflag=skip_bytes,count_bytes skip=67108864 count="$1" 2> /dev/null \
        && sh tools/sfs2.sh unpack "$out/img.after" "$2" > /dev/null 2>&1
}

section "cc: 駆動役が我々の OS の上で C を翻訳する (docs/stage017-cc.md)"

# 平らな像を OS の実行形式へ組み直したものが ELF であること。
# **ここを取り違えると spawn が ENOEXEC で落ちる**
ok=0
for f in pp16cmd cc15pcmd ld16cmd cc17 cc18 ar17 mk17; do
    [ "$(od -An -c -N 4 "tmp/build/$f" | tr -d ' ')" = '177ELF' ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: pp16cmd / cc15pcmd / ld16cmd / cc17 / cc18 / ar17 / mk17 が ELF である"

r=$out/root
mkroot "$r"
cp tests/stage017/user/hello.c "$r/hello.c"
cp tests/stage017/user/selfhost.c "$r/selfhost.c"
cat > "$r/go.sh" <<'EOF'
cc -o hello hello.c
echo "hello-build $?"
hello
echo "hello-run $?"
cc -o selfhost selfhost.c
echo "selfhost-build $?"
selfhost
echo "selfhost-run $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot "$r" "$out/basic.out"
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/basic.txt "$out/basic.out" \
    > "$out/basic.diff"
report $? "run: cc が hello と selfhost を組み，どちらも正しく走る"
[ -s "$out/basic.diff" ] && sed -n '4,$p' "$out/basic.diff"

# 期待値の側も本物と突き合わせる。**selfhost.c はホストの cc でも同じ
# 答えを出すはずである** —— 出さないなら，我々の処理系か期待値の
# どちらかが間違っている
if command -v gcc > /dev/null 2>&1; then
    gcc -w -o "$out/selfhost.host" tests/stage017/user/selfhost.c 2> /dev/null \
        && "$out/selfhost.host" > "$out/selfhost.host.out"
    r2=$?
    sed -n '/^selfhost-build 0$/,/^selfhost-run 0$/p' \
        tests/stage017/expected/basic.txt \
        | sed '1d;$d' > "$out/selfhost.want"
    [ "$r2" -eq 0 ] && diff -q "$out/selfhost.want" "$out/selfhost.host.out" \
        > /dev/null
    report $? "ref: selfhost.c の期待値がホストの cc の出力と一致する"
else
    echo "   skip: ホストに gcc が無い (期待値の裏取りができない)"
fi

section "configure が我々のコンパイラを見つける (docs/stage017-cc.md 4 章)"

tccdir=docs/external/tcc
if [ ! -d "$tccdir" ]; then
    echo "   skip: $tccdir が無い (sh tools/fetch.sh tcc で取得できる)"
    echo "   **第 1 部の完了条件はこの節である。CI では走らない**"
else
    r=$out/croot
    mkroot "$r"
    cp "$tccdir/configure" "$tccdir/VERSION" "$tccdir/conftest.c" "$r/"
    cat > "$r/go.sh" <<'EOF'
cc -o conftest conftest.c
echo "conftest-build $?"
conftest compiler
conftest version
conftest minor
conftest bigendian
sh2 configure --cc=cc --cpu=x86_64
echo "configure-rc $?"
grep CC= config.mak
grep CC_NAME config.mak
EOF
    printf 'sh2 go.sh\n' > "$r/boot"
    runroot "$r" "$out/cfg.out"
    rc=$?
    [ "$rc" -eq 0 ] && diff -u tests/stage017/expected/cfg.txt "$out/cfg.out" \
        > "$out/cfg.diff"
    report $? "run: configure が cc を検出し，最後まで通る"
    [ -s "$out/cfg.diff" ] && sed -n '4,$p' "$out/cfg.diff"

    # **検出が本当に成功したことを別の角度から見る。** configure は
    # コンパイラを見つけられなかったとき cc_name を空のままにし，
    # config.mak には gcc と書く。unknown と書かれているということは，
    # conftest を実際に組んで走らせ，その答えを読んだということである
    grep -q '^CC_NAME=unknown$' "$out/cfg.out"
    report $? "run: CC_NAME が unknown (conftest を実際に走らせた証拠)"
fi

section "第 2 部: 複数の翻訳単位と書庫 (docs/stage017-cc.md 7 章)"

# **我々の OS が，複数のファイルからなるプログラムを書庫経由でビルドする** (7.6)。
#
#   cc18 -c で 2 つの .c を .o にし
#   ar rcs で書庫にまとめ
#   cc18 が main.c + 書庫 をリンクする
#
# 書庫の員 (addx / mulx / lenx / catx) と /lib の員 (printf / strcat) の
# **両方が引けること**を見る。-I は /include に無いヘッダを拾わせ，
# -D は VERSION を外から与える (定義が無ければ翻訳が落ちるので，
# -D が効いていることが結果に出る)
r=$out/mroot
mkroot "$r"
mkdir -p "$r/inc"
cp tests/stage017/user/multi.c tests/stage017/user/mathx.c \
   tests/stage017/user/strx.c "$r/"
cp tests/stage017/user/inc/*.h "$r/inc/"
cat > "$r/go.sh" <<'EOF'
cc18 -c -I inc mathx.c -o mathx.o
echo "compile-mathx $?"
cc18 -c -I inc strx.c -o strx.o
echo "compile-strx $?"
ar rcs libx.a mathx.o strx.o
echo "ar-rcs $?"
ar t libx.a
cc18 -I inc -DVERSION=7 -o multi multi.c libx.a
echo "link $?"
multi
echo "run $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot "$r" "$out/multi.out"
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/multi.txt "$out/multi.out" \
    > "$out/multi.diff"
report $? "run: cc18 と ar が複数の翻訳単位を書庫経由でビルドする"
[ -s "$out/multi.diff" ] && sed -n '4,$p' "$out/multi.diff"

# **期待値の中の「プログラムの出力」はホストの cc でも同じはずである。**
# 出さないなら，我々の処理系か期待値のどちらかが間違っている
if command -v gcc > /dev/null 2>&1; then
    ( cd "$r" && gcc -w -I inc -DVERSION=7 -o "$OLDPWD/$out/multi.host" \
        multi.c mathx.c strx.c ) 2> /dev/null \
        && "$out/multi.host" > "$out/multi.host.out"
    r2=$?
    sed -n '/^link 0$/,/^run 0$/p' tests/stage017/expected/multi.txt \
        | sed '1d;$d' > "$out/multi.want"
    [ "$r2" -eq 0 ] && diff -q "$out/multi.want" "$out/multi.host.out" > /dev/null
    report $? "ref: multi.c の期待値がホストの cc の出力と一致する"
else
    echo "   skip: ホストに gcc が無い (期待値の裏取りができない)"
fi

# **我々の OS が作った書庫を，本物の ar が読めるか** (7.3)。
# 相互運用を見る。バイト一致は狙わない
unpackback 16777216 "$out/back"
r2=$?
if [ "$r2" -ne 0 ] || [ ! -f "$out/back/libx.a" ]; then
    report 1 "interop: OS が作った書庫を取り出せる"
elif ! command -v ar > /dev/null 2>&1; then
    echo "   skip: host に ar が無い (相互運用を確かめられない)"
else
    ar t "$out/back/libx.a" > "$out/hostar.t" 2>&1
    printf 'mathx.o\nstrx.o\n' > "$out/hostar.want"
    diff -q "$out/hostar.want" "$out/hostar.t" > /dev/null
    report $? "interop: 本物の ar t が我々の書庫の並びを列挙する"

    mkdir -p "$out/xx" && (cd "$out/xx" && rm -f ./*.o && ar x "../back/libx.a")
    cmp -s "$out/xx/mathx.o" "$out/back/mathx.o" \
        && cmp -s "$out/xx/strx.o" "$out/back/strx.o"
    report $? "interop: 本物の ar x が取り出した員が元とバイト一致する"

    # **索引を書いていないことを確かめる** (7.4)。書いたことにして
    # いないか，こちらから見にいく
    ! ar t "$out/back/libx.a" 2> /dev/null | grep -q '^/'
    report $? "spec: 符号の索引は書いていない (7.4 のとおり)"
fi


section "第 2 部: libc 自身を書庫にする (docs/stage017-cc.md 7.6)"

# **7.6 の完了条件そのものである。** 我々の OS が
#
#   1. libc の第 18 世代の 8 本を自分のコンパイラで .o にし
#   2. 自分の ar で 1 つの書庫にまとめ
#   3. その書庫だけを頼りにプログラムをリンクして走らせる
#
# /lib に置くのは走り時の下働き (rt64 / rtfp) だけにする。libc の .o を
# 置いたままだと，書庫の員と /lib の員が同じ符号を二重に定義して ld が
# 落ちる。**書庫から本当に引けていることを見るための配置である**
r=$out/lroot
rm -rf "$r"
mkdir -p "$r/bin" "$r/include/sys" "$r/lib" "$r/src" "$r/posix"
cp tmp/build/pp16cmd "$r/bin/pp16"
cp tmp/build/cc15pcmd "$r/bin/cc15p"
cp tmp/build/ld16cmd "$r/bin/ld16"
cp stage016/libc18/include/*.h "$r/include/"
cp stage016/libc18/include/sys/*.h "$r/include/sys/"
cp tmp/build/rt64.o tmp/build/rtfp.o "$r/lib/"
cp stage016/libc18/src/*.c "$r/src/"
cp stage016/libc18/posix/*.c "$r/posix/"
cp tmp/build/cc18 "$r/cc18"
cp tmp/build/ar17 "$r/ar"
cp tmp/build/sh2.bin "$r/sh2"
cp tests/stage017/user/uselibc.c "$r/uselibc.c"
cat > "$r/go.sh" <<'EOF'
for f in src/string src/stdlib src/misc15 posix/sys posix/morecore posix/stdio posix/assert posix/dir
do
  cc18 -c $f.c -o $f.o
  echo "cc $f $?"
done
ar rcs libc.a src/string.o src/stdlib.o src/misc15.o posix/sys.o posix/morecore.o posix/stdio.o posix/assert.o posix/dir.o
echo "ar $?"
ar t libc.a
cc18 -o uselibc uselibc.c libc.a
echo "link $?"
uselibc
echo "run $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot "$r" "$out/libc.out" 33554432 512
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/libc.txt "$out/libc.out" \
    > "$out/libc.diff"
report $? "run: libc の 8 本が書庫になり，それだけでリンクが通る"
[ -s "$out/libc.diff" ] && sed -n '4,$p' "$out/libc.diff"

# **員が 8 つ揃っていること**を書庫の側からも見る。kernel22 は 9 語目
# から先を黙って捨てていたので，ここは 5 員で通ってしまっていた (8.2)
if unpackback 33554432 "$out/lback" && [ -f "$out/lback/libc.a" ] \
        && command -v ar > /dev/null 2>&1; then
    ar t "$out/lback/libc.a" > "$out/libcar.t" 2>&1
    printf 'string.o\nstdlib.o\nmisc15.o\nsys.o\nmorecore.o\nstdio.o\nassert.o\ndir.o\n' \
        > "$out/libcar.want"
    diff -q "$out/libcar.want" "$out/libcar.t" > /dev/null
    report $? "interop: 本物の ar t が 8 員すべてを列挙する"
else
    echo "   skip: 書庫を取り出せない (host に ar が無いか展開に失敗)"
fi

section "引数の上限は黙って超えない (docs/stage017-cc.md 8.2)"

# kernel23 の上限は 64 語である。**溢れたら E2BIG で落ちる**ことを見る。
# 落ちずに成功が返るなら，捨てられた語がどこかで静かに効いている
r=$out/aroot
mkroot "$r"
cat > "$r/args.c" <<'EOF'
#include <stdio.h>
int main(int argc, char **argv) {
  printf("argc %d last %s\n", argc, argv[argc - 1]);
  return 0;
}
EOF
{
    printf 'cc18 -o args args.c\n'
    printf 'echo "build $?"\n'
    # 32 語 (実行名 + 31)。kernel22 なら 8 で切られる
    printf 'args'
    i=1
    while [ "$i" -le 31 ]; do printf ' a%d' "$i"; i=$((i + 1)); done
    printf '\n'
    printf 'echo "many $?"\n'
    # 96 語。64 を超えるので E2BIG で落ちるはず
    printf 'args'
    i=1
    while [ "$i" -le 95 ]; do printf ' b%d' "$i"; i=$((i + 1)); done
    printf '\n'
    printf 'echo "toomany $?"\n'
} > "$r/go.sh"
printf 'sh2 go.sh\n' > "$r/boot"
runroot "$r" "$out/args.out"
rc=$?

[ "$rc" -eq 0 ] && grep -q '^argc 32 last a31$' "$out/args.out"
report $? "run: 32 語の引数が 1 語も欠けずに届く"

# 溢れた側は「成功しない」ことを見る。sh2 が返す番号そのものは
# 問わない (spawn の失敗をどう写すかは別の話) が，**0 であっては
# ならない**。0 なら黙って捨てたということである
[ "$rc" -eq 0 ] && ! grep -q '^toomany 0$' "$out/args.out"
report $? "run: 64 語を超える呼び出しは成功を返さない"
[ "$rc" -eq 0 ] && ! grep -q '^argc 96 ' "$out/args.out"
report $? "run: 溢れた呼び出しはそもそも走っていない"

section "第 3 部の 1: make が libc を組む (docs/stage017-cc.md 9.5)"

# **第 3 部の 1 の完了条件である。** 第 2 部では go.sh に 8 行並べて
# 書いていたものを，型規則と自動変数で書き直した記述 (tests/stage017/mk/
# libc.mk) を mk に食わせる。同じことを 1 つの規則で言えることが，
# make を持った意味である。
#
# /lib に置くのは走り時の下働きだけ (第 2 部と同じ配置)。書庫から
# 本当に引けていなければリンクが落ちる
r=$out/kroot
rm -rf "$r"
mkdir -p "$r/bin" "$r/include/sys" "$r/lib" "$r/src" "$r/posix"
cp tmp/build/pp16cmd "$r/bin/pp16"
cp tmp/build/cc15pcmd "$r/bin/cc15p"
cp tmp/build/ld16cmd "$r/bin/ld16"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp stage016/libc18/include/*.h "$r/include/"
cp stage016/libc18/include/sys/*.h "$r/include/sys/"
cp tmp/build/rt64.o tmp/build/rtfp.o "$r/lib/"
cp stage016/libc18/src/*.c "$r/src/"
cp stage016/libc18/posix/*.c "$r/posix/"
cp tmp/build/cc18 "$r/cc18"
cp tmp/build/ar17 "$r/ar"
cp tmp/build/mk17 "$r/mk"
cp tmp/build/sh2.bin "$r/sh2"
cp tests/stage017/user/uselibc.c "$r/uselibc.c"
cp tests/stage017/mk/libc.mk "$r/Makefile"
cat > "$r/go.sh" <<'EOF'
mk
echo "mk $?"
uselibc
echo "run $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot "$r" "$out/mk.out" 33554432 512
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/mk.txt "$out/mk.out" \
    > "$out/mk.diff"
report $? "run: mk が Makefile どおりに libc を組み，繋いだものが走る"
[ -s "$out/mk.diff" ] && sed -n '4,$p' "$out/mk.diff"

# **同じ記述を本物の make に食わせたとき，命令の並びが一致すること。**
# 出来上がりだけ見ていると，別の順で別のものを作っても気づけない
if command -v make > /dev/null 2>&1 && command -v gcc > /dev/null 2>&1; then
    d=$out/mkdry
    rm -rf "$d"
    mkdir -p "$d/src" "$d/posix"
    cp tests/stage017/mk/libc.mk "$d/Makefile"
    touch "$d/uselibc.c"
    for f in src/string src/stdlib src/misc15 posix/sys posix/morecore \
             posix/stdio posix/assert posix/dir; do
        touch "$d/$f.c"
    done
    gcc -w -o "$out/mkhost" tests/stage017/host/mkhost.c 2> /dev/null
    r2=$?
    ( cd "$d" && make -n --no-print-directory ) > "$out/mkdry.ref" 2>&1
    [ "$r2" -eq 0 ] && ( cd "$d" && "$OLDPWD/$out/mkhost" -n ) \
        > "$out/mkdry.got" 2>&1
    [ "$r2" -eq 0 ] && diff -q "$out/mkdry.ref" "$out/mkdry.got" > /dev/null
    report $? "ref: 本物の make と命令の並びが一致する (libc.mk)"
    [ -s "$out/mkdry.got" ] && diff -u "$out/mkdry.ref" "$out/mkdry.got" \
        | sed -n '4,$p'
else
    echo "   skip: host に make か gcc が無い (並びの裏取りができない)"
fi

# **関数は黙って空に展開しない** (9.5)。第 3 部の 2 まで落ちること
if [ -x "$out/mkhost" ]; then
    d=$out/mkfn
    rm -rf "$d" && mkdir -p "$d"
    printf 'A = $(subst x,y,axb)\nall:\n\t@echo $(A)\n' > "$d/Makefile"
    ( cd "$d" && "$OLDPWD/$out/mkhost" ) > "$out/mkfn.out" 2>&1
    [ $? -ne 0 ] && grep -q 'functions are not implemented' "$out/mkfn.out"
    report $? "spec: 未実装の関数は空に展開せず落ちる (9.5)"
fi

section "第 4 部の 1: sfs3 が時刻を持つ (docs/stage017-cc.md 11 章)"

# ファイルの mtime を "上位語 下位語" の形で出す。**期待値は手で書かず
# ここで作る** —— 手で書いた値は，ホストと OS の両方が間違っていても
# 気づけないうえ，実際に 1 度書き間違えた
hilo() {
    _v=$(stat -c '%.9Y' "$1")
    _s=${_v%.*}; _f=${_v#*.}
    _f=$(printf '%s' "$_f" | sed 's/^0*//')
    [ -n "$_f" ] || _f=0
    _ns=$((_s * 1000000000 + _f))
    printf '%s %s' $((_ns / 4294967296)) $((_ns % 4294967296))
}

# 根を sfs3 で詰めて kernel24 で走らせる。$1=根 $2=出力 $3=大きさ $4=件数
#
# **記憶は 512M でなければならない。** kernel19 以降はユーザの
# フレームスタックの上端を USP = 0x9700_0000 に置いており，256M
# (0x8000_0000〜0x9000_0000) では届かない。届かないと最初の spawn が
# 積む引数の書込みでストアアクセス例外 (mcause 7) になり，
# **何も出さずに落ちる** (docs/stage016-os.md 8 章)
runroot3() {
    sh tools/sfs3.sh pack "$1" "$out/i3" "${3:-4194304}" "${4:-128}" \
            > /dev/null \
        && rm -f "$out/r3" \
        && dd if=/dev/null of="$out/r3" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/i3" of="$out/r3" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_RAMFILE="$out/r3" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel24.bin < /dev/null \
            > "$2" 2>&1
}

# **第 4 部の 1 の完了条件である。**
#
# ホストが既知の mtime を与えたファイルを sfs3 で詰め，kernel24 の上で
# stamp に読ませて，**ホストが与えた値とナノ秒まで一致すること**を見る。
#
# 時刻はわざと「ありえない過去」に置く。**今の時刻をそのまま使うと，
# カーネルが表から読んだのか，カーネルが立て直したのか区別が付かない**
r=$out/troot
rm -rf "$r"
mkdir -p "$r/bin" "$r/d"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp tmp/build/sh2.bin "$r/sh2"
cp tmp/build/stamp "$r/stamp"
printf 'hello\n'   > "$r/a.txt"        # 6 バイト
printf 'worldly\n' > "$r/d/b.txt"      # 8 バイト
: > "$r/zero"                          # 0 バイト
touch -d '@1700000000.123456789' "$r/a.txt"
touch -d '@1600000123.000000001' "$r/d/b.txt"
touch -d '@1500000000.999999999' "$r/zero"
touch -d '@1400000000.500000000' "$r/d"
cat > "$r/go.sh" <<'EOF'
stamp a.txt d/b.txt zero d
echo "stamp $?"
stamp nosuch
echo "missing $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"

{
    printf 'f 6 %s a.txt\n' "$(hilo "$r/a.txt")"
    printf 'f 8 %s d/b.txt\n' "$(hilo "$r/d/b.txt")"
    printf 'f 0 %s zero\n' "$(hilo "$r/zero")"
    printf 'd 0 %s d\n' "$(hilo "$r/d")"
    printf 'stamp 0\n'
    printf '? nosuch\n'
    printf 'missing 1\n'
} > "$out/stamp.want"

runroot3 "$r" "$out/stamp.out"
rc=$?
[ "$rc" -eq 0 ] && diff -u "$out/stamp.want" "$out/stamp.out" > "$out/stamp.diff"
report $? "run: ホストが詰めた時刻を OS が読み，ナノ秒まで一致する"
[ -s "$out/stamp.diff" ] && sed -n '4,$p' "$out/stamp.diff"

# **OS が書いたファイルには今の時刻が立つこと。** 読めるだけでは
# 足りない —— 立てる側が働いていなければ差分ビルドはできない
r=$out/wroot
rm -rf "$r"
mkdir -p "$r/bin"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp tmp/build/sh2.bin "$r/sh2"
cp tmp/build/stamp "$r/stamp"
printf 'old\n' > "$r/old.txt"
touch -d '@1500000000.000000000' "$r/old.txt"
cat > "$r/go.sh" <<'EOF'
echo made > new.txt
stamp old.txt new.txt
EOF
printf 'sh2 go.sh\n' > "$r/boot"
oldwant="f 4 $(hilo "$r/old.txt") old.txt"
before=$(date +%s)
runroot3 "$r" "$out/wstamp.out"
rc=$?
after=$(date +%s)

[ "$rc" -eq 0 ] && grep -qxF "$oldwant" "$out/wstamp.out"
report $? "run: 触っていないファイルの時刻は変わらない"
[ "$rc" -eq 0 ] && grep -qxF "$oldwant" "$out/wstamp.out" \
    || { echo "   want: $oldwant"; sed -n '1,4p' "$out/wstamp.out"; }

# 新しいほうは**この実行の間の時刻**であること。秒へ直して
# before <= t <= after を見る (前後 2 秒の余裕を取る)
newline=$(grep ' new.txt$' "$out/wstamp.out" 2> /dev/null || true)
if [ -n "$newline" ]; then
    hi=$(echo "$newline" | cut -d' ' -f3); lo=$(echo "$newline" | cut -d' ' -f4)
    sec=$(( (hi * 4294967296 + lo) / 1000000000 ))
    [ "$sec" -ge $((before - 2)) ] && [ "$sec" -le $((after + 2)) ]
    report $? "run: OS が書いたファイルに今の時刻が立つ"
    [ "$sec" -ge $((before - 2)) ] && [ "$sec" -le $((after + 2)) ] \
        || echo "   before=$before sec=$sec after=$after"
else
    report 1 "run: OS が書いたファイルに今の時刻が立つ"
fi

# ホスト側の詰め直し。**中身だけ見ていると 11 章で踏んだ誤りを見落とす**
s3=$out/s3rt
rm -rf "$s3" "$s3.back"
mkdir -p "$s3/sub"
printf 'x\n' > "$s3/f1"; printf 'yy\n' > "$s3/sub/f2"
touch -d '@1234567890.111111111' "$s3/f1"
touch -d '@1111111111.222222222' "$s3/sub/f2"
touch -d '@1000000000.333333333' "$s3/sub"
sh tools/sfs3.sh pack "$s3" "$out/rt.img" 262144 32 > /dev/null 2>&1 \
    && sh tools/sfs3.sh unpack "$out/rt.img" "$s3.back" > /dev/null 2>&1
r2=$?
[ "$r2" -eq 0 ] && diff -r "$s3" "$s3.back" > /dev/null 2>&1
report $? "roundtrip: sfs3 に詰めて展開すると中身が元と一致する"
ok=0
for f in f1 sub/f2 sub; do
    [ "$(stat -c '%.9Y' "$s3/$f" 2> /dev/null)" \
      = "$(stat -c '%.9Y' "$s3.back/$f" 2> /dev/null)" ] || ok=1
done
[ "$r2" -eq 0 ] && [ "$ok" -eq 0 ]
report $? "roundtrip: 時刻もナノ秒まで一致する (ディレクトリを含む)"

section "第 4 部の 2: mk が差分ビルドする (docs/stage017-cc.md 11.6)"

# **第 4 部の 2 の完了条件である。**
#
# 我々の OS の上で mk を 2 度走らせ，2 度目が何も作らないこと。そのあと
# 依存を 1 つだけ触って 3 度目を走らせ，**触ったものに繋がる分だけ**が
# 作り直されること。
#
# 触るのに使うのは sh2 の `>` である (OS の中で時刻を立て直す)。
# ホストで触ってから詰め直すと，**カーネルが時刻を立てているのか
# ホストが立てた時刻を読んでいるだけなのか区別が付かない**
r=$out/iroot
rm -rf "$r"
mkdir -p "$r/bin"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp tmp/build/sh2.bin "$r/sh2"
cp tmp/build/mk18 "$r/mk"
printf 'A\n' > "$r/a.src"
printf 'B\n' > "$r/b.src"
touch -d '@1500000000.000000000' "$r/a.src" "$r/b.src"
cat > "$r/Makefile" <<'MK'
all: prog

%.cp: %.src
	cat $< > $@

prog: a.cp b.cp
	cat $^ > $@
MK
cat > "$r/go.sh" <<'EOF'
echo "--1--"
mk
echo "--2--"
mk
echo "--3--"
echo C > b.src
mk
echo "--4--"
mk -B
echo "--end--"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot3 "$r" "$out/inc.out"
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/inc.txt "$out/inc.out" \
    > "$out/inc.diff"
report $? "run: 2 度目は作らず，触った依存に繋がる分だけ作り直す"
[ -s "$out/inc.diff" ] && sed -n '4,$p' "$out/inc.diff"

# **同じ筋道を本物の make でも辿ること。** 我々の判定だけが正しく
# 見えていても意味が無い
if command -v make > /dev/null 2>&1 && command -v gcc > /dev/null 2>&1; then
    gcc -w -o "$out/mk18host" tests/stage017/host/mk18host.c 2> /dev/null
    r2=$?
    d=$out/incdry
    for who in ref got; do
        rm -rf "$d"
        mkdir -p "$d"
        cp "$r/Makefile" "$d/Makefile"
        printf 'A\n' > "$d/a.src"; printf 'B\n' > "$d/b.src"
        {
            if [ "$who" = ref ]; then
                ( cd "$d" && make --no-print-directory )
                echo "--2--"
                ( cd "$d" && make --no-print-directory )
                echo "--3--"
                sleep 1; printf 'C\n' > "$d/b.src"
                ( cd "$d" && make --no-print-directory )
            else
                ( cd "$d" && "$OLDPWD/$out/mk18host" )
                echo "--2--"
                ( cd "$d" && "$OLDPWD/$out/mk18host" )
                echo "--3--"
                sleep 1; printf 'C\n' > "$d/b.src"
                ( cd "$d" && "$OLDPWD/$out/mk18host" )
            fi
        } 2>&1 | grep -v "^make: \|^mk: " > "$out/inc.$who"
    done
    [ "$r2" -eq 0 ] && diff -q "$out/inc.ref" "$out/inc.got" > /dev/null
    report $? "ref: 本物の make と，走る命令の並びが 3 回とも一致する"
    [ "$r2" -eq 0 ] && diff -u "$out/inc.ref" "$out/inc.got" | sed -n '4,$p'
else
    echo "   skip: host に make か gcc が無い (裏取りができない)"
fi

section "第 3 部の 2: make の関数 (docs/stage017-cc.md 9.1)"

# **第 3 部の 2 の完了条件である。**
#
# 第 3 部の 1 では目的ファイルを 8 個手で並べていた。ここでは
# **元のファイルの並びから $(patsubst) で導く** —— 同じことを言うのに
# 手で並べ直さなくてよくなるのが，関数を持った意味である。
r=$out/froot
rm -rf "$r"
mkdir -p "$r/bin" "$r/include/sys" "$r/lib" "$r/src" "$r/posix"
cp tmp/build/pp16cmd "$r/bin/pp16"
cp tmp/build/cc15pcmd "$r/bin/cc15p"
cp tmp/build/ld16cmd "$r/bin/ld16"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp stage016/libc18/include/*.h "$r/include/"
cp stage016/libc18/include/sys/*.h "$r/include/sys/"
cp tmp/build/rt64.o tmp/build/rtfp.o "$r/lib/"
cp stage016/libc18/src/*.c "$r/src/"
cp stage016/libc18/posix/*.c "$r/posix/"
cp tmp/build/cc18 "$r/cc18"
cp tmp/build/ar17 "$r/ar"
cp tmp/build/mk19 "$r/mk"
cp tmp/build/sh2.bin "$r/sh2"
cp tests/stage017/user/uselibc.c "$r/uselibc.c"
cp tests/stage017/mk/fn.mk "$r/Makefile"
cat > "$r/go.sh" <<'EOF'
mk show
mk
echo "mk $?"
uselibc
echo "run $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot3 "$r" "$out/fn.out" 33554432 512
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/fn.txt "$out/fn.out" \
    > "$out/fn.diff"
report $? "run: 関数で導いた並びから libc が組め，繋いだものが走る"
[ -s "$out/fn.diff" ] && sed -n '4,$p' "$out/fn.diff"

# **同じ記述を本物の make に食わせて突き合わせる。** 関数は結果が
# 静かに空になる形の誤りが出やすいので，並びそのものを見る
if command -v make > /dev/null 2>&1 && command -v gcc > /dev/null 2>&1; then
    gcc -w -o "$out/mk19host" tests/stage017/host/mk19host.c 2> /dev/null
    r2=$?
    d=$out/fndry
    rm -rf "$d"
    mkdir -p "$d/src" "$d/posix"
    cp tests/stage017/mk/fn.mk "$d/Makefile"
    touch "$d/uselibc.c"
    for f in src/string src/stdlib src/misc15 posix/sys posix/morecore \
             posix/stdio posix/assert posix/dir; do
        touch "$d/$f.c"
    done
    ( cd "$d" && make -n --no-print-directory; make -s show ) \
        > "$out/fndry.ref" 2>&1
    [ "$r2" -eq 0 ] && ( cd "$d" && "$OLDPWD/$out/mk19host" -n; \
        "$OLDPWD/$out/mk19host" -s show ) > "$out/fndry.got" 2>&1
    [ "$r2" -eq 0 ] && diff -q "$out/fndry.ref" "$out/fndry.got" > /dev/null
    report $? "ref: 本物の make と，関数の展開と命令の並びが一致する"
    [ "$r2" -eq 0 ] && diff -u "$out/fndry.ref" "$out/fndry.got" | sed -n '4,$p'

    # **知らない関数は空に展開せず落ちること** (9.5 からの決まり)
    rm -rf "$out/fnbad" && mkdir -p "$out/fnbad"
    printf 'all:\n\t@echo $(notdir a/b.c)\n' > "$out/fnbad/Makefile"
    ( cd "$out/fnbad" && "$OLDPWD/$out/mk19host" ) > "$out/fnbad.out" 2>&1
    [ $? -ne 0 ] && grep -q 'unknown function' "$out/fnbad.out"
    report $? "spec: 知らない関数は空に展開せず落ちる"
else
    echo "   skip: host に make か gcc が無い (裏取りができない)"
fi

section "第 3 部の 3 の 1: tcc の Makefile の形 (docs/stage017-cc.md 15.1)"

# **第 3 部の 3 の 1 の完了条件である。**
#
# 完了条件は「tcc の Makefile が読めること」ではない —— **そこから出る
# 命令の並びが本物の make と一致すること**である。読めることと正しく
# 読めることは違う。実際，読めてはいたが $< が別のファイルを指し，
# 目標特有の += が変数を凍らせていた (15.3)。
#
# 本物の tcc の Makefile での突き合わせは下の 15.2 に置いた手順で行う。
# 素材 (docs/external/tcc) は repo に入れない決まりなので CI では走らない。
# ここでは tcc の Makefile が使う 6 つの形を自前で並べた記述を使う
r=$out/kroot
rm -rf "$r"
mkdir -p "$r/bin" "$r/src" "$r/lib"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp tmp/build/sh2.bin "$r/sh2"
cp tmp/build/mk20 "$r/mk"
cp tmp/build/mk20 "$r/bin/mk"
cp tests/stage017/mk/tccish-os.mk "$r/Makefile"
cp tests/stage017/mk/tccish-oslib.mk "$r/lib/Makefile"
printf 'BASE\n'  > "$r/base.txt"
printf 'EXTRA\n' > "$r/extra.txt"
printf 'M\n'     > "$r/src/main.src"
printf 'U\n'     > "$r/src/util.src"
printf 'H\n'     > "$r/config.h"
printf 'A\n'     > "$r/asm.asm"
printf 'a\n'     > "$r/lib/a.src"
printf 'b\n'     > "$r/lib/b.src"
touch -d '@1500000000.000000000' "$r/base.txt" "$r/extra.txt" \
    "$r/src/main.src" "$r/src/util.src" "$r/config.h" "$r/asm.asm" \
    "$r/lib/a.src" "$r/lib/b.src"
cat > "$r/go.sh" <<'EOF'
echo "--dry--"
mk -n
echo "--build--"
mk
echo "--again--"
mk
echo "--touch--"
echo U2 > src/util.src
mk
echo "--prog--"
cat prog
echo "--libx--"
cat lib/libx
echo "--end--"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot3 "$r" "$out/tccish.out"
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/tccish.txt "$out/tccish.out" \
    > "$out/tccish.diff"
report $? "run: 6 つの形が我々の OS の上で順に効く (再帰する MAKE を含む)"
[ -s "$out/tccish.diff" ] && sed -n '4,$p' "$out/tccish.diff"

# **本物の make と突き合わせる。** 我々の -n の出力だけを見ていても，
# それが正しいかどうかは判らない。ホストに素材があるときはここで見る
if command -v make > /dev/null 2>&1 && command -v gcc > /dev/null 2>&1; then
    gcc -w -o "$out/mk20host" tests/stage017/host/mk20host.c 2> /dev/null
    r2=$?
    d=$out/tidry
    rm -rf "$d"
    mkdir -p "$d/src" "$d/lib"
    cp tests/stage017/mk/tccish.mk "$d/Makefile"
    cp tests/stage017/mk/tccish-lib.mk "$d/lib/Makefile"
    # zmain は **並びを見るため**に置く。readdir の順は作った順なので，
    # 名前の順に直していなければここで食い違う (15.3)
    touch "$d/src/zmain.c" "$d/src/util.c" "$d/src/main.c" \
          "$d/config.h" "$d/asm.S" "$d/lib/a.c" "$d/lib/b.c"
    ( cd "$d" && make -n --no-print-directory all lib ) > "$out/ti.ref" 2>&1
    if [ "$r2" -eq 0 ]; then
        ( cd "$d" && "$OLDPWD/$out/mk20host" -n all lib ) > "$out/ti.raw" 2>&1
        # $(MAKE) は自分自身なので綴りが違う。そこだけ揃える
        sed "s#$OLDPWD/$out/mk20host -n#make#g" "$out/ti.raw" > "$out/ti.got"
    fi
    [ "$r2" -eq 0 ] && diff -q "$out/ti.ref" "$out/ti.got" > /dev/null
    report $? "ref: 本物の make と，出る命令の並びが一致する"
    [ "$r2" -eq 0 ] && diff -u "$out/ti.ref" "$out/ti.got" | sed -n '4,$p'
else
    echo "   skip: host に make か gcc が無い (裏取りができない)"
fi

section "第 3 部の 3 の 2: -I を探す道として持つ (docs/stage017-cc.md 19 章)"

# **第 3 部の 3 の 2 の要である。**
#
# ヘッダを inc/ にだけ置き，束ねには入れない。cc18 の「-I は階層ごと
# 束ねる」ではこれも通るが，tcc の木では員が上限を超えて落ちる (16.2)。
# cc19 は -I を pp17 へ渡し，pp17 が要るものだけを開く。
#
# **道は変えたが出るものは変わっていないこと**を，cc18 の出力と
# バイトで突き合わせて見る。
#
# 併せて 19.3 の破壊が戻っていないことを見る。cc18 は -o を付けた
# 結合で「最後に走査したヘッダ」を .o で潰していた。**症状が出ない**
# 壊れ方なので，出力ではなく**入力が無傷であること**を見るしかない。
r=$out/proot
rm -rf "$r"
mkdir -p "$r/bin" "$r/include/sys" "$r/lib" "$r/inc"
cp tmp/build/pp16cmd "$r/bin/pp16"
cp tmp/build/pp17 "$r/bin/pp17"
cp tmp/build/cc15pcmd "$r/bin/cc15p"
cp tmp/build/ld16cmd "$r/bin/ld16"
cp tmp/build/sh2.bin "$r/bin/sh2"
cp tmp/build/sh2.bin "$r/sh2"
cp stage017/libc19/include/*.h "$r/include/"
cp stage017/libc19/include/sys/*.h "$r/include/sys/"
cp tmp/build/l19_src_string.o tmp/build/l19_src_stdlib.o \
   tmp/build/l19_src_misc15.o tmp/build/l19_posix_sys.o \
   tmp/build/l19_posix_morecore.o tmp/build/l19_posix_stdio.o \
   tmp/build/l19_posix_assert.o tmp/build/l19_posix_dir.o \
   tmp/build/rt64.o tmp/build/rtfp.o "$r/lib/"
cp tmp/build/cc18 "$r/cc18"
cp tmp/build/cc19 "$r/cc19"
printf '#define BASE 40\n' > "$r/inc/conf.h"
printf '#include "conf.h"\n#define TOTAL (BASE + 2)\n' > "$r/inc/extra.h"
cat > "$r/main.c" <<'CEOF'
#include <stdio.h>
#include "extra.h"
int main(void) { printf("total %d\n", TOTAL); return 0; }
CEOF
cat > "$r/go.sh" <<'EOF'
cc18 -c main.c -o m18.o -I inc
echo "cc18 $?"
cc19 -c main.c -o m19.o -I inc
echo "cc19 $?"
cc19 -o prog main.c -I inc
echo "link $?"
prog
echo "run $?"
cc19 -c main.c -o bad.o
echo "noinc $?"
EOF
printf 'sh2 go.sh\n' > "$r/boot"
runroot3 "$r" "$out/pi.out" 8388608 512
rc=$?
[ "$rc" -eq 0 ] && diff -u tests/stage017/expected/incpath.txt "$out/pi.out" \
    > "$out/pi.diff"
report $? "run: ヘッダが -I にしか無くても cc19 が翻訳・結合し，走る"
[ -s "$out/pi.diff" ] && sed -n '4,$p' "$out/pi.diff"

# 走った後の像を戻して中身を見る
if [ "$rc" -eq 0 ]; then
    dd if="$out/r3" of="$out/pi.img" bs=64K skip=1024 2> /dev/null
    rm -rf "$out/piback"
    sh tools/sfs3.sh unpack "$out/pi.img" "$out/piback" > /dev/null 2>&1
fi

# **束ねる道と探す道が同じ .o を出すこと** (19.2 の C)
[ "$rc" -eq 0 ] && cmp -s "$out/piback/m18.o" "$out/piback/m19.o"
report $? "same: cc18 (束ねる) と cc19 (探す道) が同じ .o を出す"

# **入力が無傷であること** (19.3 の破壊が戻っていないこと)
ok=0
for f in inc/conf.h inc/extra.h main.c include/sys/time.h include/sys/stat.h; do
    if ! cmp -s "$r/$f" "$out/piback/$f"; then
        ok=1
        echo "   壊れた: $f ($(wc -c < "$r/$f") -> $(wc -c < "$out/piback/$f" 2> /dev/null) バイト)"
    fi
done
[ "$rc" -eq 0 ] && [ "$ok" -eq 0 ]
report $? "spec: 結合しても入力のヘッダとソースが 1 バイトも変わらない (19.3)"

# .o が名前どおりの場所にできていること (潰した先へ書いていない証拠)
[ "$rc" -eq 0 ] && [ -s "$out/piback/_t0.o" ]
report $? "spec: 中間の .o が _t0.o として在る (19.3)"

section "第 3 部の 3 の 2 の完了条件: tcc の Makefile を回す (21 章)"

# **これが第 3 部の 3 の 2 の完了条件である。**
#
# 上の節までは「同じ形が同じに読める」「-I が探す道として効く」を
# 見ているだけで，**本物の tcc の Makefile を回してはいない**。
# 素材 (docs/external/tcc) は repo に入れない決まりなので CI では
# 取得できず，ここは走らない。手元では必ず走らせること。
#
#   sh tools/fetch.sh tcc      素材を取る
#   sh tools/tcc.sh src        patch を当てる
#   sh tools/tcc17.sh all      11 本を訳す (1 本ずつ。中断しても続きから)
#   sh tools/tcc17.sh mk       **Makefile を mk20 に読ませて回す**
#   sh tools/tcc17.sh check    出来た tcc に実際に翻訳させる
#
# 21.2 の突き合わせ 2 つ (Makefile から出た tcc == 手で回した tcc /
# 我々の c2str が作った tccdefs_.h == ホストのもの) までを見ること。
if [ ! -d docs/external/tcc ]; then
    echo "   skip: docs/external/tcc が無い (sh tools/fetch.sh tcc で取得できる)"
    echo "   **第 3 部の 3 の 2 の完了条件はこの節である。CI では走らない**"
    echo "   手元では sh tools/tcc17.sh all && sh tools/tcc17.sh mk を走らせること"
elif [ ! -s tmp/s17/tcc-mk ] || [ ! -s tmp/s17/tcc ] \
    || [ ! -s tmp/s17/hello.o ] || [ ! -s tmp/s17/back/t/tccdefs_.h ]; then
    # **見るものが揃っていなければ飛ばす。** 前提を 1 つだけ見て断言を
    # 3 つ立てていたので，手で別の実験をした後に素の FAIL になった。
    # 「材料が無い」と「壊れている」は別である
    echo "   skip: tmp/s17 の材料が揃っていない"
    echo "   **第 3 部の 3 の 2 の完了条件はこの節である**"
    echo "   sh tools/tcc17.sh all && sh tools/tcc17.sh link \\"
    echo "     && sh tools/tcc17.sh mk && sh tools/tcc17.sh check"
else
    # **同じものが出ること。** 道が 2 つあって違うものが出るなら，
    # どちらかが間違っている。
    #
    # **両方を同じ回で作ること。** 片方だけ作り直すと出所の違うものを
    # 比べることになり，中身の話でないところで落ちる (実際に落とした)
    cmp -s tmp/s17/tcc tmp/s17/tcc-mk
    report $? "same: Makefile から出た tcc と，命令を直に並べた tcc がバイト一致"
    # **我々の OS の上で作った tccdefs_.h** がホストのものと一致すること
    cmp -s tmp/s17/back/t/tccdefs_.h tmp/tcc/build/tccdefs_.h
    report $? "same: 我々の c2str が作った tccdefs_.h がホストのものとバイト一致"
    # 出来た tcc が実際に翻訳できること
    [ -s tmp/s17/hello.o ]
    report $? "run: 我々の OS の上で組んだ tcc が hello.o を出した (tcc17.sh check)"
fi

section "第 3 部の 3 の 3: libtcc1.a (手元でのみ走る)"

# 出来た tcc が lib/Makefile を回して libtcc1.a を作れること。
# ここも素材が要るので CI では走らない。
#
#   sh tools/tcc17.sh lib
#
# **ファイルが出たことを完了条件にしてはいけない。** 実際に，員の
# 見出しが 2 進数のまま 50,412 バイトの「書庫でないファイル」が出て
# いた (docs/stage017-cc.md 27〜28 章)。見るのは ar17 が読めた員の
# 並びである
LIBTCC1_MEMBERS="libtcc1.o riscv32.o stdatomic.o atomic.o builtin.o
alloca.o alloca-bt.o tcov.o armflush.o dsohandle.o"

if [ ! -d docs/external/tcc ]; then
    echo "   skip: docs/external/tcc が無い (sh tools/fetch.sh tcc で取得できる)"
    echo "   **第 3 部の 3 の 3 の完了条件はこの節である。CI では走らない**"
elif [ ! -s tmp/s17/libtcc1.a ]; then
    # **飛ばしてよいのは「書庫がまだ無い」ときだけ。** 書庫が在るのに
    # 員の並びが無い (ar17 が読めなかった) のは壊れているということで，
    # それは飛ばさず落とす —— **「材料が無い」と「壊れている」は別**
    echo "   skip: tmp/s17 の材料が揃っていない"
    echo "   sh tools/tcc17.sh all && sh tools/tcc17.sh link \\"
    echo "     && sh tools/tcc17.sh mk && sh tools/tcc17.sh lib"
else
    # 我々の ar17 が，我々の tcc が作った書庫を読めること。
    # 10 本すべてが，Makefile の並びどおりに出ること。
    # libtcc1.list が空 (読めなかった) ならここで落ちる
    printf '%s\n' $LIBTCC1_MEMBERS > tmp/s17/libtcc1.want
    cmp -s tmp/s17/libtcc1.list tmp/s17/libtcc1.want
    report $? "lib: 我々の OS の上で組んだ tcc の -ar が作った libtcc1.a を ar17 が読め，員が 10 本そろっている"
    # **最後まで通ること。** libtcc1.a の後にも作るものがある (runmain.o)。
    # 逆進 (backtrace) を切る前は bt-exe.c で止まっていた (29 章)
    grep -q '^lib 0$' tmp/s17/lib.log
    report $? "lib: lib/Makefile が最後まで通る (mk -C t/lib が rc 0)"
fi

summary
