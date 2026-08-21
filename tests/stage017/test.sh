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
    cp tmp/build/sh2.bin "$_r/sh2"
}

# 根を像にして kernel22 で走らせる。出力は $2 へ
runroot() {
    sh tools/sfs2.sh pack "$1" "$out/img" 16777216 256 > /dev/null \
        && rm -f "$out/ram" \
        && dd if=/dev/null of="$out/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$out/img" of="$out/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_RAMFILE="$out/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu tmp/build/kernel22.bin < /dev/null \
            > "$2" 2>&1
}

section "cc: 駆動役が我々の OS の上で C を翻訳する (docs/stage017-cc.md)"

# 平らな像を OS の実行形式へ組み直したものが ELF であること。
# **ここを取り違えると spawn が ENOEXEC で落ちる**
ok=0
for f in pp16cmd cc15pcmd ld16cmd cc17; do
    [ "$(od -An -c -N 4 "tmp/build/$f" | tr -d ' ')" = '177ELF' ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: pp16cmd / cc15pcmd / ld16cmd / cc17 が ELF である"

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

summary
