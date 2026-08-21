#!/bin/sh
# 生成物のビルド。成果物は tmp/build/ (git ignore) に置く。
# 各 Stage の成果物は前段の成果物のみでビルドする (docs/plan.md 2.1)。
#
# 使用法: build.sh [stage002|...|stage013|stage014|all]
#
# キャッシュ (スタンプ):
#   ビルドは決定的である (同じ入力から常に同じバイト列が生成される。
#   各 Stage のテストが SHA-256 の照合と固定点で保証している)。したがって
#   入力が前回と一致する Stage は作り直さなくてよい。
#   各 Stage の tmp/build/<stage>.stamp に「入力のハッシュ」と「生成物の
#   sha256sum」を記録し，両方が一致すればその Stage を省略する。
#   入力に前段のスタンプを含めることで，上流の変更は下流全体へ伝播する。
#   STONE_FORCE_BUILD=1 でスタンプを無視して作り直す (dev-notes.md 1.3)。
#
# 各 Stage のビルド手順と入力・生成物の宣言は tools/build/<stage>.sh に
# 分けてある。**その Stage のスタンプの入力はその 1 ファイルだけ**なので，
# ある Stage の手順を直しても他の Stage は作り直されない。
# (本ファイル 1 枚に全 Stage の手順が入っていた頃は，どこを触っても
# 全段が作り直しになり，CI がジョブの時間上限を超えた。)
#
# **本ファイル (駆動側) はスタンプの入力に含めていない。** run_stage の
# 意味を変えるような改訂をしたときは STONE_FORCE_BUILD=1 で作り直すこと。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
mkdir -p tmp/build

# 生成物が「無い」「0 バイト」でないことを見る。0 なら 1 を返す。
#
# **これを入れたのは，落ちたビルドが緑として記録される事故が 3 度
# 起きたからである** (docs/dev-notes.md 1.2)。仕組みはこうである ——
# ccgen_run / kern_run / *_run はどれも末尾が `echo "built ..." >&2` で，
# **関数の終了状態はその echo のもの**になる。途中の qemu が 1 つ残らず
# 失敗しても，リダイレクトが 0 バイトのファイルを作り，関数は 0 を返し，
# step がそれを正しい生成物として印を書く。次回は「できている」と
# 誤認して先へ進む。
#
# 各 *_run に set -e を入れて回るより，**出来上がりを見る**ほうが確実で
# ある。この鎖の生成物に 0 バイトが正しい場面は無い。
nonempty() {
    for _f in "$@"; do
        if [ ! -s "$_f" ]; then
            echo "error: $_f が空か存在しない (ビルドは失敗している)" >&2
            return 1
        fi
    done
    return 0
}

# run_stage <stage> <生成物 (tmp/build/ 内の名前)...> -- <入力ファイル...>
run_stage() {
    name=$1; shift
    outs=""
    while [ "$1" != -- ]; do outs="$outs tmp/build/$1"; shift; done
    shift
    stamp=tmp/build/$name.stamp
    new=$(sha256sum "$@" | sha256sum | cut -d' ' -f1)
    if [ -z "${STONE_FORCE_BUILD:-}" ] && [ -f "$stamp" ] \
        && [ "$(head -n 1 "$stamp")" = "$new" ] \
        && tail -n +2 "$stamp" | sha256sum -c --status - 2>/dev/null; then
        echo "cached $name (stamp: 入力と生成物が前回と一致)" >&2
        return 0
    fi
    # ビルドが落ちたらスタンプを書かない。書いてしまうと次回は
    # 「できている」と誤認して先へ進み，原因から遠い所で落ちる
    if ! "build_$name"; then
        echo "error: build_$name が失敗した" >&2
        rm -f "$stamp"
        return 1
    fi
    # shellcheck disable=SC2086
    if ! nonempty $outs; then
        echo "error: build_$name の生成物が空だった" >&2
        rm -f "$stamp"
        return 1
    fi
    { echo "$new"; sha256sum $outs; } > "$stamp"
}

# ---- 成果物ごとのスタンプ (step) ----
#
# run_stage は Stage 単位なので，**途中で殺されるとその Stage の頭から
# やり直し**になる。Stage 15 は 30 分を超えるので，これが実務上いちばん
# 痛い (コンテナの再起動・CI のタイムアウト・Ctrl-C のどれでも起きる)。
#
# step は成果物 1 つ (あるいは 1 世代) ごとにスタンプを持ち，既にできて
# いるものを飛ばす。**殺されても失うのは高々 1 世代 (2〜3 分) である。**
# run_stage の外側のスタンプはそのまま残る。全部できていれば外側で
# 一発で飛ぶので，温まった回の費用は変わらない。
#
#   step <名前> <生成物...> -- <入力...> -- <コマンド...>
step() {
    _name=$1; shift
    _outs=""
    while [ "$1" != -- ]; do _outs="$_outs tmp/build/$1"; shift; done
    shift
    _ins=""
    while [ "$1" != -- ]; do _ins="$_ins $1"; shift; done
    shift
    _stamp=tmp/build/step-$_name.stamp
    # shellcheck disable=SC2086
    _new=$(sha256sum $_ins 2> /dev/null | sha256sum | cut -d' ' -f1)
    if [ -z "${STONE_FORCE_BUILD:-}" ] && [ -f "$_stamp" ] \
        && [ "$(head -n 1 "$_stamp")" = "$_new" ] \
        && tail -n +2 "$_stamp" | sha256sum -c --status - 2> /dev/null; then
        echo "cached $_name" >&2
        return 0
    fi
    "$@" || return 1
    # shellcheck disable=SC2086
    nonempty $_outs || return 1
    # shellcheck disable=SC2086
    { echo "$_new"; sha256sum $_outs; } > "$_stamp"
}

# cc の世代を 1 つ作る (2 段。1 段目で作った器で自分自身を作り直す)。
#   ccgen <名前> <前段の bin> <ソース> [<リンカの bin>]
# 名前はすべて拡張子なしで受ける (cc15a / cc14g / ld)。.bin はここで付ける
ccgen() {
    step "$1" "${1}0.bin" "${1}.bin" \
        -- "$3" "tmp/build/${2}.bin" "tmp/build/${4:-ld}.bin" \
        -- ccgen_run "$1" "$2" "$3" "${4:-ld}"
}

ccgen_run() {
    { cat "$3"; printf '\004'; } \
        | sh tools/env.sh qemu "tmp/build/${2}.bin" > "tmp/build/${1}0.o"
    { cat "tmp/build/${1}0.o"; printf '\0'; } \
        | sh tools/env.sh qemu "tmp/build/${4}.bin" > "tmp/build/${1}0.bin"
    echo "built tmp/build/${1}0.bin (bootstrap)" >&2
    { cat "$3"; printf '\004'; } \
        | sh tools/env.sh qemu "tmp/build/${1}0.bin" > "tmp/build/${1}.o"
    { cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu "tmp/build/${4}.bin" > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

# 道具を 1 つ作る (1 段。pp / ld のように自分自身では作らないもの)。
#   tool1 <名前> <翻訳する cc の bin> <ソース> [<リンカの bin>]
tool1() {
    step "$1" "${1}.bin" \
        -- "$3" "tmp/build/${2}.bin" "tmp/build/${4:-ld}.bin" \
        -- tool1_run "$1" "$2" "$3" "${4:-ld}"
}

tool1_run() {
    { cat "$3"; printf '\004'; } \
        | sh tools/env.sh qemu "tmp/build/${2}.bin" > "tmp/build/${1}.o"
    { cat "tmp/build/${1}.o"; printf '\0'; } \
        | sh tools/env.sh qemu "tmp/build/${4}.bin" > "tmp/build/${1}.bin"
    echo "built tmp/build/${1}.bin" >&2
}

# 各 Stage の手順を読み込む (build_<stage> と do_<stage> を定義する)
for f in "$repo_root"/tools/build/stage*.sh; do
    . "$f"
done

stages="stage002 stage003 stage004 stage005 stage006 stage007 stage008 stage009 stage010 stage011 stage012 stage013 stage014 stage015 stage016 stage017"
target=${1:-all}
[ "$target" = all ] && target=stage017
case " $stages " in
*" $target "*) ;;
*)
    echo "usage: build.sh [stage002|...|stage015|stage016|all]" >&2
    exit 2
    ;;
esac
# stage010 までは 1 本の鎖 (各段が直前の段の成果物を使う) なので順に作る
for s in stage002 stage003 stage004 stage005 stage006 stage007 stage008 \
         stage009 stage010; do
    "do_$s"
    [ "$s" = "$target" ] && exit 0
done

# stage011 以降は依存が分かれる (do_* の入力宣言のとおり，stage011 -> stage012
# の鎖と stage013・stage014 は，いずれも stage010 までの成果物とスタンプに
# しか依らない)。全段が対象のときは 3 本に分けて並列に作る。各 Stage の
# 生成物とスタンプは互いに素なので，並列にしても出来るバイト列は変わらない。
# 途中の Stage までの指定は従来どおり順に作る
if [ "$target" != stage014 ] && [ "$target" != stage015 ] \
    && [ "$target" != stage016 ] && [ "$target" != stage017 ]; then
    for s in stage011 stage012 stage013; do
        "do_$s"
        [ "$s" = "$target" ] && break
    done
    exit 0
fi
( do_stage011 && do_stage012 ) & lane1=$!
do_stage013 & lane2=$!
# stage015 は stage014 の成果物 (cc14g) を使うので同じ本の中で順に作る。
# stage016 は stage015 の最前線 (cc15p / pp16 / ld16) を使うのでその後ろ。
# stage017 は stage016 の libc18 を使うのでさらにその後ろ
if [ "$target" = stage017 ]; then
    ( do_stage014 && do_stage015 && do_stage016 && do_stage017 ) & lane3=$!
elif [ "$target" = stage016 ]; then
    ( do_stage014 && do_stage015 && do_stage016 ) & lane3=$!
elif [ "$target" = stage015 ]; then
    ( do_stage014 && do_stage015 ) & lane3=$!
else
    do_stage014 & lane3=$!
fi
rc=0
wait "$lane1" || rc=1
wait "$lane2" || rc=1
wait "$lane3" || rc=1
exit "$rc"
