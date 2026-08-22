#!/bin/sh
# 中断からの再開 (docs/dev-notes.md 1.7)。
#
# 使用法:
#   resume.sh            状態を揃えるだけ (何をしたかを出す)
#   resume.sh --build    揃えてからビルドを続きから走らせる
#   resume.sh --test     揃えてから検査を走らせる (ビルドも含む)
#   resume.sh --check    直すべき点を並べるだけで，何も変更しない
#
# 作業環境が巻き戻る (コンテナが古い snapshot へ戻る) ことがある。
# **失われるのは push していないものすべてである** —— 作業木の変更・
# `tmp/build` の生成物・`tools/env.sh` に当てた局所のパッチ。git の
# 遠隔だけが唯一の永続する置き場である。
#
# 手で戻すと 4 手かかり，順序を間違えると壊れる。実際に間違えて
# **0 バイトの生成物を全段に撒いた**ことがある (1.2.1)。手順を 1 つに
# まとめる。
#
# ---- 何をするか ----
#
#   1. 追跡している遠隔の枝と突き合わせ，**遅れているときだけ**揃える
#   2. `tmp/build` の 0 バイトの生成物を消す (巻き戻りが残すことがある)
#   3. STONE_ENGINE=host のときだけ，1.2 のホスト実行パッチを当て直す
#   4. --build / --test があれば続きから走らせる (スタンプが効く)
#
# ---- しないこと ----
#
# **push していない commit があるときは何もしない。** 巻き戻りなら
# HEAD は遠隔より遅れているだけなので，進んでいるなら巻き戻りでは
# ない。取り違えて捨てると取り返しがつかない。
#
# `tools/env.sh` 以外に作業木の変更があるときも止まる。巻き戻り以外の
# 状況で走らせてしまった可能性があるからである。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

do_build=0
do_test=0
check_only=0
for a in "$@"; do
    case "$a" in
    --build) do_build=1 ;;
    --test)  do_test=1 ;;
    --check) check_only=1 ;;
    *) echo "usage: resume.sh [--build] [--test] [--check]" >&2; exit 2 ;;
    esac
done

say() { echo "resume: $*" >&2; }

# ---- 1. 遠隔と突き合わせる ----

branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$branch" = HEAD ]; then
    say "detached HEAD なので枝の同期は飛ばす"
else
    upstream=$(git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2> /dev/null || true)
    if [ -z "$upstream" ]; then
        say "$branch に追跡先が無いので枝の同期は飛ばす"
    else
        git fetch --quiet "${upstream%%/*}" "${upstream#*/}" 2> /dev/null || \
            say "fetch に失敗した (遠隔に届かない)。手元の状態のまま続ける"
        local_sha=$(git rev-parse HEAD)
        remote_sha=$(git rev-parse "$upstream" 2> /dev/null || echo "$local_sha")

        if [ "$local_sha" = "$remote_sha" ]; then
            say "枝は $upstream と一致している"
        elif [ -n "$(git rev-list "$upstream..HEAD" 2> /dev/null)" ]; then
            # 遠隔に無い commit を持っている = 巻き戻りではない
            n=$(git rev-list --count "$upstream..HEAD")
            say "**push していない commit が $n 個ある。何も変更しない。**"
            say "  巻き戻りなら HEAD は遠隔より遅れているだけである。"
            say "  先に push するか，意図を確かめること"
            exit 1
        else
            # 作業木の状態を見る。env.sh 以外に**追跡下の**変更があれば止まる。
            # 追跡外のファイル (??) は checkout -B が保つので邪魔をしない
            dirty=$(git status --porcelain | grep -v '^??' | awk '{print $2}' \
                | grep -v '^tools/env\.sh$' || true)
            if [ -n "$dirty" ]; then
                say "**tools/env.sh 以外に作業木の変更がある。何も変更しない。**"
                echo "$dirty" | sed 's/^/  /' >&2
                exit 1
            fi
            n=$(git rev-list --count "HEAD..$upstream")
            if [ "$check_only" -eq 1 ]; then
                say "枝が $upstream より $n 個遅れている (--check なので揃えない)"
            else
                say "枝が $upstream より $n 個遅れている。揃える"
                # skip-worktree が立っていると checkout が拒む。外して戻す
                git update-index --no-skip-worktree tools/env.sh 2> /dev/null || true
                git checkout -- tools/env.sh 2> /dev/null || true
                git checkout -B "$branch" "$upstream" > /dev/null 2>&1
                say "  $(git rev-parse --short HEAD) へ揃えた"
            fi
        fi
    fi
fi

# ---- 2. 0 バイトの生成物を消す ----
#
# 巻き戻りが 0 バイトの生成物を残すことがある。印は 0 バイトの
# SHA-256 とも一致するので，放っておくとキャッシュの照合を通って
# しまう (1.2.1)。tools/build.sh の nonempty が印を拒むようになって
# いるが，消しておけば作り直しが素直に走る
if [ -d tmp/build ]; then
    empty=$(find tmp/build -type f -size 0 2> /dev/null | wc -l | tr -d ' ')
    if [ "$empty" -gt 0 ]; then
        if [ "$check_only" -eq 1 ]; then
            say "tmp/build に 0 バイトの生成物が $empty 個ある (--check なので消さない)"
        else
            say "tmp/build の 0 バイトの生成物を $empty 個消す"
            find tmp/build -type f -size 0 -delete 2> /dev/null || true
        fi
    fi
fi

# ---- 3. ホスト実行のパッチ (STONE_ENGINE=host のときだけ) ----
#
# **パッチ本文は docs/dev-notes.md 1.2 から読む。** ここに書き写すと
# 「リポジトリにホスト実行の分岐を持つ」ことになり，1.2 の禁止事項を
# 名前を変えて破ることになる。文書に載っているものを当てるだけなら，
# 手でやっていた作業をそのまま自動にしただけである。
#
# 当てた後は skip-worktree を立てる。**commit されないことが要点**で，
# tools/test.sh が HEAD 側を見て見張っている
patch_env() {
    if grep -q 'STONE_ENGINE:-}" = host' tools/env.sh; then
        say "ホスト実行のパッチは既に当たっている"
        git update-index --skip-worktree tools/env.sh 2> /dev/null || true
        return 0
    fi
    _p=$(awk '
        /^```sh$/ { inblk = 1; buf = ""; next }
        /^```$/   {
            if (inblk && buf ~ /STONE_ENGINE:-\}" = host/) { printf "%s", buf; exit }
            inblk = 0; next
        }
        inblk { buf = buf $0 "\n" }
    ' docs/dev-notes.md)
    if [ -z "$_p" ]; then
        say "**docs/dev-notes.md 1.2 からパッチを取り出せなかった**"
        return 1
    fi
    if [ "$check_only" -eq 1 ]; then
        say "ホスト実行のパッチが当たっていない (--check なので当てない)"
        return 0
    fi
    # set -eu の直後へ差し込む
    _tmp=$(mktemp)
    awk -v patch="$_p" '
        { print }
        !done && /^set -eu$/ { print ""; printf "%s\n", patch; done = 1 }
    ' tools/env.sh > "$_tmp"
    if ! sh -n "$_tmp"; then
        say "**当てた結果が構文として通らない。捨てる**"
        rm -f "$_tmp"
        return 1
    fi
    cat "$_tmp" > tools/env.sh
    rm -f "$_tmp"
    git update-index --skip-worktree tools/env.sh 2> /dev/null || true
    say "ホスト実行のパッチを当てた (**局所のみ。commit しない**)"
}

if [ "${STONE_ENGINE:-}" = host ]; then
    patch_env
else
    if grep -q 'STONE_ENGINE:-}" = host' tools/env.sh 2> /dev/null; then
        say "作業木の tools/env.sh にホスト実行のパッチが当たっている"
        say "  (STONE_ENGINE=host でないので触らない。commit しないこと)"
    fi
fi

# ---- 4. 続きから走らせる ----

if [ "$check_only" -eq 1 ]; then
    say "--check なので走らせない"
    exit 0
fi

if [ "$do_test" -eq 1 ]; then
    say "検査を走らせる (スタンプの効くところは飛ばす)"
    exec bash tools/test.sh
elif [ "$do_build" -eq 1 ]; then
    say "ビルドを続きから走らせる"
    exec sh tools/build.sh all
fi

say "状態を揃えた。--build / --test で続きから走らせられる"
