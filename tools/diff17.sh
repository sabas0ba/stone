#!/bin/sh
# 我々の鎖とホストの処理系に**同じソースを**訳させ，走らせて
# **値を突き合わせる** (docs/stage017-gcc.md 5.2)。
#
#   sh tools/diff17.sh          プローブ全部 (前置部の側と OS の側)
#   sh tools/diff17.sh bare     前置部だけで走る側 (Stage 15 の器の適合)
#   sh tools/diff17.sh os       libc を繋いで OS の上で走らせる側 (Stage 17)
#   sh tools/diff17.sh <名前>   1 つだけ
#
# ## なぜ要るか
#
# `cc15u` (複合代入が符号を見ていない) は，**往復検査でも固定点でも
# 再現性でもバイト一致でも捕まらなかった**。捕まえたのは
# 「我々が書いていない物差し」だけである。
#
# 台帳 (tests/stage015/ledger.txt) の期待値は**我々が書いている**ので，
# 我々の思い込みがそのまま期待値になる。zlib の adler32 を突き合わせて
# 初めて出たのがその証拠である。**同じことを 1 回限りの手作業ではなく
# 仕組みにする**のがここである。
#
# ## 見るのは 2 通り
#
#   両方訳せた   標準出力を突き合わせる。違えば**我々の側を疑う**
#   我々が拒んだ ホストが**診断を出すか**を見る。出さないなら
#                「C が許す形を拒んでいる」ことになる
#
# 2 つ目が要るのは，台帳の `gap` に 2 つの意味があるからである ——
# 「まだ実装していない」と「誤った入力を正しく拒む」。後者を名乗るには
# **その入力が本当に誤っていること**を我々以外が言っている必要がある。
#
# ## ホストは万能の物差しではない
#
# 語長が違う (ホストは 64 bit)。C が定義していない振舞い (0 除算) は
# 比べようがない。鎖の内部だけの名前を呼ぶプローブもある。**飛ばす
# ものは名前と理由を必ず出す** —— 黙って飛ばすと「全部合った」に見える。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

out=tmp/d17
mkdir -p "$out"

cc=${STONE_DIFF_CC:-tmp/build/cc15aa.bin}  # 最前線の世代で測る
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
prb=tests/stage015/probe
hdr=stage015/libc/include/stdarg.h
shim=tests/hostshim/shim.h

# ---- OS の側 (libc を繋いで我々の OS の上で走らせる) ----
#
# 前置部だけで走る形では libc を測れない。**libc の穴は我々が書いた
# 期待値では出ない** —— 我々は自分が使う書き方しか試さないからである
# (docs/stage017-gcc.md 5.3)。
osprb=tests/stage017/probe
ospp=tmp/build/pp16.bin
osld=tmp/build/ld17.bin           # 落ちたら名前を言うリンカ (5.1)
oskern=tmp/build/kernel24.bin
# 測る libc の世代。**前の世代を測り直せるようにしてある** ——
# 「直す前は何が違っていたか」を後から再現できないと，直した記録が
# 我々の言い分だけになる (STONE_DIFF_LIBCGEN=21 で第 21 世代)
osgen=${STONE_DIFF_LIBCGEN:-22}
LIBC=stage017/libc$osgen
# /lib と同じ組合せ。src/morecore と posix/morecore は同じ符号を
# 別の環境向けに定義するので，どちらか一方だけ (tools/ext17.sh と同じ)
OSLIB=$(for u in src_string src_ctype src_stdlib src_misc15 \
                 posix_sys posix_morecore posix_stdio posix_assert \
                 posix_dir posix_signal; do
            printf 'l%s_%s ' "$osgen" "$u"
        done; printf 'rt64 rtfp')
# Stage 15 のプローブのうち libc を要るもの。**世代をまたぐので明示する**
OSPRB_EXTRA=strtod

HOSTCC=${CC:-gcc}

# **飛ばすものと理由。** 黙って飛ばさない
skip_reason() {
    case $1 in
    fpsoft)
        echo "鎖の内部の名前 (__dadd / __dmul …) を直に呼ぶ。ホストに実体が無い" ;;
    layout-oracle)
        echo "main を持たない断片 (layout.c の期待値を出すためのもの)" ;;
    hyg16)
        echo "前処理器の検査。訳して走らせるものではない (既に gcc の cpp と突き合わせている)" ;;
    lldiv)
        echo "0 除算を見る。C が定義していない振舞いなのでホストは SIGFPE で落ちる" ;;
    strsizeof)
        echo "sizeof p == 4 を主張する。ホストは 64 bit なので語長で必ず違う" ;;
    layout)
        echo "構造体の配置を RV32 の規則で主張する。ホストは x86-64 の規則" ;;
    *)  echo "" ;;
    esac
}

# **C89 の外側を測るプローブ。** 我々が拒んだときの裏取りは
# `gcc -std=c89 -pedantic-errors` に「その入力は誤りか」を訊く形だが，
# C99 や GNU の書き方は C89 モードのホストなら**当たり前に拒む**ので，
# この裏取りが素通しになる —— 我々が実装できていないだけの拒否が
# 「拒むのが正しい」に化ける。
#
# ここに名前があるプローブは，**我々が拒んだ時点で落とす**
# (docs/stage018-ext.md 4)。
beyond_c89() {
    case $1 in
    c99decl) return 0 ;;
    desig)   return 0 ;;
    stmtexpr) return 0 ;;
    typeofx) return 0 ;;
    caserange) return 0 ;;
    esac
    return 1
}

pass=0; fail=0; skipped=0

one() {
    n=$1
    why=$(skip_reason "$n")
    if [ -n "$why" ]; then
        printf 'skip %-14s %s\n' "$n" "$why"
        skipped=$((skipped + 1))
        return 0
    fi

    # ---- 我々の側 ----
    ourc=0
    sh tools/bundle.sh "$hdr" "$prb/$n.c" 2> /dev/null \
        | sh tools/env.sh qemu "$pp" > "$out/$n.i" 2> /dev/null || {
        printf 'FAIL %-14s 我々の pp が落ちた\n' "$n"
        fail=$((fail + 1)); return 1
    }
    sh tools/env.sh qemu "$cc" < "$out/$n.i" > "$out/$n.o" 2> /dev/null
    ourc=$?
    if [ "$ourc" -eq 0 ]; then
        { cat "$out/$n.o" tmp/build/rt64.o tmp/build/rtfp.o; printf '\0'; } \
            | sh tools/env.sh qemu "$ld" > "$out/$n.bin" 2> /dev/null || {
            printf 'FAIL %-14s 我々の ld が落ちた\n' "$n"
            fail=$((fail + 1)); return 1
        }
        ourout=$(sh tools/env.sh qemu "$out/$n.bin" < /dev/null 2> /dev/null)
    fi

    # ---- ホストの側 ----
    "$HOSTCC" -w -include "$shim" -o "$out/h_$n" "$prb/$n.c" -lm \
        > "$out/h_$n.log" 2>&1
    hostc=$?

    if [ "$ourc" -ne 0 ] && beyond_c89 "$n"; then
        # **C89 の外側を測るプローブは，拒んだ時点で落とす。**
        # C89 モードのホストに訊いても「誤り」と言うに決まっているので，
        # 裏取りにならない
        printf 'FAIL %-14s 我々が拒む (rc=%s)。C89 の外側を受ける約束のプローブである\n' \
            "$n" "$ourc"
        fail=$((fail + 1)); return 1
    fi

    if [ "$ourc" -ne 0 ]; then
        # **我々が拒んだ。** 拒むのが正しいと言えるのは，その入力が
        # 本当に C89 として誤っているときだけである。それを我々以外に
        # 言わせる。
        #
        # **警告の数を数えるのでは弱い。** gcc は正しいソースにも
        # -Wmissing-braces のような書き方の助言を出すので，
        # 「警告が出た = 誤った入力」にはならない。`-std=c89
        # -pedantic-errors` で**エラーになるか**を見る —— こちらは
        # 制約違反にしか出ない。
        #
        # なお -w は付けない。付けると診断そのものが消えて
        # 「ホストは何も言わなかった」という誤った結論になる
        "$HOSTCC" -std=c89 -pedantic-errors -include "$shim" \
            -fsyntax-only "$prb/$n.c" > "$out/p_$n.log" 2>&1
        pedc=$?
        if [ "$pedc" -ne 0 ]; then
            printf 'ok   %-14s 我々は拒む (rc=%s)。C89 として誤り (%s)\n' \
                "$n" "$ourc" \
                "$(grep -m1 -oE 'error: .*' "$out/p_$n.log")"
            pass=$((pass + 1)); return 0
        fi
        printf 'FAIL %-14s 我々だけが拒む (rc=%s)。ホストは C89 として通す\n' \
            "$n" "$ourc"
        fail=$((fail + 1)); return 1
    fi

    if [ "$hostc" -ne 0 ]; then
        printf 'FAIL %-14s 我々は通すがホストが翻訳できない (%s の先頭を見よ)\n' \
            "$n" "$out/h_$n.log"
        fail=$((fail + 1)); return 1
    fi

    hostout=$(timeout 30 "$out/h_$n" < /dev/null 2> /dev/null)
    hrc=$?
    if [ "$hrc" -ne 0 ]; then
        printf 'FAIL %-14s ホストの実行が rc=%s で落ちた\n' "$n" "$hrc"
        fail=$((fail + 1)); return 1
    fi

    if [ "$ourout" = "$hostout" ]; then
        printf 'ok   %-14s 値が一致 [%s]\n' "$n" "$ourout"
        pass=$((pass + 1)); return 0
    fi
    printf 'FAIL %-14s 値が違う\n' "$n"
    printf '       我々  [%s]\n' "$ourout"
    printf '       ホスト [%s]\n' "$hostout"
    fail=$((fail + 1)); return 1
}

# ================= OS の側 =================
#
# 前置部だけで走る形 (上) は libc を持たない。ここは **libc を繋いで
# 我々の OS (kernel24) の上で走らせ**，同じソースをホストで走らせた
# 標準出力と突き合わせる。
#
# 起動は 1 回だけである。プローブごとに QEMU を上げ下げすると，測る
# ものより待つ時間のほうが長くなる。1 つの像に全部詰め，シェル (sh2)
# に順に起動させて，`@@ 名前` の行で出力を切り分ける。

osout=$out/os

os_src() {
    if [ -f "$osprb/$1.c" ]; then echo "$osprb/$1.c"; else echo "$prb/$1.c"; fi
}

os_names() {
    for f in "$osprb"/*.c; do basename "$f" .c; done
    for n in $OSPRB_EXTRA; do echo "$n"; done
}

# 要る生成物が揃っているか。**足りないものは名前で言う** ——
# 「OS 側は 0 件でした」と静かに終わるのがいちばん悪い
os_missing() {
    _m=""
    for f in "$cc" "$ospp" "$osld" "$oskern" tmp/build/sh2.bin; do
        [ -s "$f" ] || _m="$_m $f"
    done
    for o in $OSLIB; do
        [ -s "tmp/build/$o.o" ] || _m="$_m tmp/build/$o.o"
    done
    echo "$_m"
}

# 我々の側で 1 本組む。返り値: 0 出来た / 3 我々の cc が拒んだ / 4 その他
os_build() {
    _n=$1
    _src=$(os_src "$_n")
    sh tools/bundle.sh "$LIBC"/include/*.h \
        "sys/time.h=$LIBC/include/sys/time.h" \
        "sys/stat.h=$LIBC/include/sys/stat.h" \
        "sys/types.h=$LIBC/include/sys/types.h" \
        "$_src" 2> /dev/null \
        | sh tools/env.sh qemu "$ospp" > "$osout/$_n.i" 2> /dev/null || return 4
    sh tools/env.sh qemu "$cc" < "$osout/$_n.i" > "$osout/$_n.o" 2> /dev/null \
        || return 3
    _objs=""
    for _o in $OSLIB; do _objs="$_objs tmp/build/$_o.o"; done
    # **落ちた ld の言い分は出力ファイルの中身である** (ld の標準出力は
    # 像なので。tools/ext17.sh do_run と同じ)。中身の有無では判らない
    # ので，見るのは終了状態のほうである
    # shellcheck disable=SC2086
    { printf 'E'; cat "$osout/$_n.o" $_objs; printf '\0'; } \
        | sh tools/env.sh qemu "$osld" > "$osout/bin/$_n" 2> /dev/null \
        || return 4
    [ -s "$osout/bin/$_n" ] || return 4
    return 0
}

# 我々が拒んだときの裏取り (上の one と同じ規則)。0 なら「拒むのが正しい」
os_reject_ok() {
    "$HOSTCC" -std=c89 -pedantic-errors -include "$shim" \
        -fsyntax-only "$(os_src "$1")" > "$osout/p_$1.log" 2>&1
}

os_run_all() {
    _names=$1
    rm -rf "$osout"
    mkdir -p "$osout/bin" "$osout/root"

    _built=""
    for _n in $_names; do
        os_build "$_n"
        _rc=$?
        if [ "$_rc" -eq 0 ]; then
            _built="$_built $_n"
            continue
        fi
        if [ "$_rc" -eq 3 ] && beyond_c89 "$_n"; then
            printf 'FAIL %-14s 我々が拒む。C89 の外側を受ける約束のプローブである\n' "$_n"
        elif [ "$_rc" -eq 3 ]; then
            if os_reject_ok "$_n"; then
                printf 'FAIL %-14s 我々だけが拒む。ホストは C89 として通す\n' "$_n"
            else
                printf 'ok   %-14s 我々は拒む。C89 として誤り (%s)\n' "$_n" \
                    "$(grep -m1 -oE 'error: .*' "$osout/p_$_n.log")"
                pass=$((pass + 1)); continue
            fi
        else
            printf 'FAIL %-14s 我々の側で組めない (%s/%s の .i と bin/%s を見よ)\n' \
                "$_n" "$osout" "$_n" "$_n"
        fi
        fail=$((fail + 1))
    done
    [ -n "$_built" ] || return 0

    # 像を詰める。**シェルと道具は我々のもの**である
    cp tmp/build/sh2.bin "$osout/root/sh2"
    : > "$osout/root/go.sh"
    for _n in $_built; do
        cp "$osout/bin/$_n" "$osout/root/$_n"
        printf 'echo @@%s\n%s\n' "$_n" "$_n" >> "$osout/root/go.sh"
    done
    printf 'echo @@end\n' >> "$osout/root/go.sh"
    printf 'sh2 go.sh\n' > "$osout/root/boot"

    sh tools/sfs3.sh pack "$osout/root" "$osout/fs.img" 16777216 256 \
            > /dev/null 2>&1 \
        && rm -f "$osout/ram" \
        && dd if=/dev/null of="$osout/ram" bs=1 seek=536870912 2> /dev/null \
        && dd if="$osout/fs.img" of="$osout/ram" bs=64K oflag=seek_bytes \
            seek=67108864 conv=notrunc 2> /dev/null \
        && STONE_QEMU_TIMEOUT=${STONE_QEMU_TIMEOUT:-600} \
            STONE_QEMU_RAMFILE="$osout/ram" STONE_QEMU_RAM=512M \
            sh tools/env.sh qemu "$oskern" < /dev/null \
            > "$osout/run.out" 2>&1
    if [ ! -s "$osout/run.out" ]; then
        for _n in $_built; do
            printf 'FAIL %-14s 我々の OS が何も出さなかった (%s)\n' "$_n" \
                "$osout/run.out"
            fail=$((fail + 1))
        done
        return 0
    fi

    # **終わりの印が無ければ途中で止まっている。** 出た分だけを見て
    # 「合っていた」と言わないための見張りである
    if ! grep -q '^@@end$' "$osout/run.out"; then
        printf 'FAIL %-14s 走行が最後まで届かなかった (%s を見よ)\n' \
            "(os)" "$osout/run.out"
        fail=$((fail + 1))
    fi

    for _n in $_built; do
        awk -v n="@@$_n" '
            $0 == n { on = 1; next }
            /^@@/   { on = 0 }
            on      { print }
        ' "$osout/run.out" > "$osout/$_n.ours"
        if ! "$HOSTCC" -w -include "$shim" -o "$osout/h_$_n" \
                "$(os_src "$_n")" -lm > "$osout/h_$_n.log" 2>&1; then
            printf 'FAIL %-14s 我々は通すがホストが翻訳できない (%s)\n' \
                "$_n" "$osout/h_$_n.log"
            fail=$((fail + 1)); continue
        fi
        # **ホスト側も作業場の中で走らせる。** ファイルを作るプローブが
        # あるので (filex)，走らせる場所を揃えないと repo が汚れるうえ，
        # 残り物が次の走行の答を変える
        if ! ( cd "$osout" && timeout 30 "./h_$_n" < /dev/null > "$_n.host" \
                2> /dev/null ); then
            printf 'FAIL %-14s ホストの実行が落ちた\n' "$_n"
            fail=$((fail + 1)); continue
        fi
        if cmp -s "$osout/$_n.ours" "$osout/$_n.host"; then
            printf 'ok   %-14s OS 側: 出力が一致 (%s 行)\n' "$_n" \
                "$(wc -l < "$osout/$_n.ours" | tr -d ' ')"
            pass=$((pass + 1))
        else
            printf 'FAIL %-14s OS 側: 出力が違う\n' "$_n"
            diff -u "$osout/$_n.host" "$osout/$_n.ours" \
                | sed -n '3,$p' | sed 's/^/       /'
            fail=$((fail + 1))
        fi
    done
}

is_os_probe() {
    for _n in $(os_names); do [ "$_n" = "$1" ] && return 0; done
    return 1
}

run_bare() {
    for f in "$prb"/*.c; do
        n=$(basename "$f" .c)
        # libc を要るものは OS 側で測る。前置部だけでは繋がらない
        case " $OSPRB_EXTRA " in *" $n "*) continue ;; esac
        one "$n"
    done
}

run_os() {
    miss=$(os_missing)
    if [ -n "$miss" ]; then
        echo "skip OS 側: 生成物が無い ($miss)"
        skipped=$((skipped + 1))
        return 0
    fi
    echo "-- OS 側 (libc を繋いで kernel24 の上で走らせる) --"
    os_run_all "$(os_names | tr '\n' ' ')"
}

case ${1:-all} in
all)  run_bare; echo; run_os ;;
bare) run_bare ;;
os)   run_os ;;
*)
    if is_os_probe "$1"; then os_run_all "$1"; else one "$1"; fi ;;
esac

echo
echo "diff17: 一致 $pass / 食い違い $fail / 飛ばした $skipped"
[ "$fail" -eq 0 ]
