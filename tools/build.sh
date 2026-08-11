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
    "build_$name"
    { echo "$new"; sha256sum $outs; } > "$stamp"
}

# 各 Stage の手順を読み込む (build_<stage> と do_<stage> を定義する)
for f in "$repo_root"/tools/build/stage*.sh; do
    . "$f"
done

stages="stage002 stage003 stage004 stage005 stage006 stage007 stage008 stage009 stage010 stage011 stage012 stage013 stage014 stage015"
target=${1:-all}
[ "$target" = all ] && target=stage015
case " $stages " in
*" $target "*) ;;
*)
    echo "usage: build.sh [stage002|...|stage014|stage015|all]" >&2
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
if [ "$target" != stage014 ] && [ "$target" != stage015 ]; then
    for s in stage011 stage012 stage013; do
        "do_$s"
        [ "$s" = "$target" ] && break
    done
    exit 0
fi
( do_stage011 && do_stage012 ) & lane1=$!
do_stage013 & lane2=$!
# stage015 は stage014 の成果物 (cc14g) を使うので同じ本の中で順に作る
if [ "$target" = stage015 ]; then
    ( do_stage014 && do_stage015 ) & lane3=$!
else
    do_stage014 & lane3=$!
fi
rc=0
wait "$lane1" || rc=1
wait "$lane2" || rc=1
wait "$lane3" || rc=1
exit "$rc"
