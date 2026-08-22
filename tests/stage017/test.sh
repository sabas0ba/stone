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
for f in pp16cmd cc15pcmd ld16cmd cc17 cc18 ar17; do
    [ "$(od -An -c -N 4 "tmp/build/$f" | tr -d ' ')" = '177ELF' ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: pp16cmd / cc15pcmd / ld16cmd / cc17 / cc18 / ar17 が ELF である"

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

summary
