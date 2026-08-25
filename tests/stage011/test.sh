#!/bin/bash
# Stage 11 テスト: フリースタンディング libc 第 1〜3 部の検証
# (docs/stage011-libc.md 5 章・7.5・8.5)。
#
#   ビルド再現   string.o / ctype.o / stdlib.o の SHA-256 が各 .md 記載値と一致する
#   値の照合     各関数を境界込みで呼び，結果を出力して照合する
#   リンクの単位 使わない翻訳単位を並べなくてもリンクが通ること
#                (str は string.o のみ，cty は ctype.o のみ，def はどちらも無し)
#   自己適用     libc を使って書いた小さなプログラム (word) が動くこと
#
# テストの素材は Stage 10 と同じく用途で分ける。
#   src/       コンパイルして実行するプログラム
#   expected/  その標準出力
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp/s11

cc=tmp/build/cc.bin
pp=tmp/build/pp.bin
ld=tmp/build/ld.bin
src=tests/stage011/src
exp=tests/stage011/expected

# ヘッダ一式と翻訳単位を束ねて pp -> cc -> ld -> 実行し，expected/ と照合する。
# 束ねには全ヘッダを入れてよい (#include されたものだけが取り込まれる)。
# リンクするオブジェクトは引数で明示し，それ以外は並べない
runcase() {
    name=$1
    shift
    objs=$*
    # 手順を関数にしておく。**打ち切られたときだけ**丸ごとやり直せるように
    # するためである (CI で 1 度，1 秒で終わるはずの str が打ち切りまで
    # 動かなかった。docs/dev-notes.md 1.6)。標準入力を使う段があるので
    # やり直しは**手順の頭から**でなければならない
    # shellcheck disable=SC2086
    do_runcase() {
        sh tools/bundle.sh stage011/libc/include/*.h "$src/$name.c" \
            | sh tools/env.sh qemu "$pp" > "tmp/s11/$name.i" \
            && sh tools/env.sh qemu "$cc" < "tmp/s11/$name.i" > "tmp/s11/$name.o" \
            && { cat "tmp/s11/$name.o" $objs; printf '\0'; } \
                | sh tools/env.sh qemu "$ld" > "tmp/s11/$name.bin" \
            && sh tools/env.sh qemu "tmp/s11/$name.bin" < /dev/null > "tmp/s11/$name.out"
    }
    stable_out "feature: $name" do_runcase
    rc=$?
    [ "$rc" -eq 0 ] && diff -q "tmp/s11/$name.out" "$exp/$name.txt" > /dev/null
    report $? "feature: $name"
}

# ---------------------------------------------------------------------------
section "ビルド再現"

ensure_build stage011
rc=$?
ok=0
[ "$rc" -eq 0 ] || ok=1
for pair in l11_string:stage011/libc/src/string.md l11_ctype:stage011/libc/src/ctype.md l11_stdlib:stage011/libc/src/stdlib.md; do
    n=${pair%%:*}
    doc=${pair##*:}
    want=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
    got=$(sha256sum "tmp/build/$n.o"); got=${got%% *}
    [ -n "$want" ] && [ "$want" = "$got" ] || ok=1
done
[ "$ok" -eq 0 ]
report $? "build: string.o / ctype.o / stdlib.o の SHA-256 が各 .md 記載値と一致"

# ---------------------------------------------------------------------------
section "値の照合とリンクの単位"

# def はヘッダだけで完結する (オブジェクトを 1 個も並べない)
runcase def
# str は string.o だけ，cty は ctype.o だけ，mal / srt / num は
# stdlib.o だけでリンクが通る
runcase str tmp/build/l11_string.o
runcase cty tmp/build/l11_ctype.o
runcase mal tmp/build/l11_stdlib.o
runcase srt tmp/build/l11_stdlib.o
runcase num tmp/build/l11_stdlib.o

# ---------------------------------------------------------------------------
section "自己適用"

# string.o と ctype.o の両方を使う
runcase word tmp/build/l11_string.o tmp/build/l11_ctype.o

summary
