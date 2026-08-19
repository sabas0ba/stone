#!/bin/bash
# リグレッションテスト (ローカル・CI 共用)。
# 環境の検証を行った後，各 Stage のテスト (tests/stage*/test.sh) を実行する。
# 各 Stage のテスト実体は当該ディレクトリ配下に置き，本スクリプトには追加しない。
#
# 使用法: test.sh [stageNNN ...]
#   引数なしなら全 Stage。引数を与えるとその Stage のテストだけを実行する。
#   ビルドは常に全段を対象にするが，スタンプ (tools/build.sh) により
#   変更の無い段は作り直されない (docs/dev-notes.md 1.3)。
#
#   テストにもスタンプがある。**入力が前回と一致し，前回通っている
#   Stage は飛ばす** (docs/dev-notes.md 1.5)。STONE_FORCE_TEST=1 で
#   無視して全部走らせる。
#
# 各 Stage のテストは並列に走らせる (共有するのは tmp/build の生成物の
# 読取りだけで，書き込み先は Stage ごとに分かれている)。出力は Stage ごとに
# tmp/test-<stage>.log へ取り，Stage の順に完了を待って表示するので，
# 一覧の見た目は逐次実行と変わらない。STONE_TEST_SERIAL=1 で従来どおり
# 1 つずつ実行する (出力がその場で流れるので，単体の追い込みに向く)。
#
# **同時に走らせる本数は CPU 数までに抑える** (STONE_TEST_JOBS で変えられる)。
# 各 Stage のテストは QEMU を次々に起動するので，本数が CPU 数を大きく
# 超えると取り合いになる。CI (2 コア) で Stage が 14 まで増えたとき，
# 負荷の高い版のコンパイルが 1 つだけ落ちる形で現れた。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

targets="$*"
for s in $targets; do
    if [ ! -f "tests/$s/test.sh" ]; then
        echo "error: tests/$s/test.sh が無い" >&2
        exit 2
    fi
done

echo "== env =="
sh tools/env.sh build > tmp/test-build.log 2>&1
rc=$?
report $rc "env: イメージビルドと packages.lock の照合 (log: tmp/test-build.log)"
if [ "$rc" -ne 0 ]; then
    # 環境が用意できなければ以降は全滅する。ログを出して早期に中止する
    echo "---- tmp/test-build.log (末尾 40 行) ----"
    tail -n 40 tmp/test-build.log
    echo "----------------------------------------"
    echo "result: environment unavailable"
    exit 1
fi
sh tools/env.sh run gdb-multiarch --version > /dev/null 2>&1
report $? "env: gdb-multiarch の導入確認"
overall_fail=$fail

# ブートストラップ鎖はここで一度だけ作る。各 Stage のテストは
# STONE_PREBUILT を見て作り直しを省く (tests/lib.sh の ensure_build)。
# ビルド再現の検査 (生成物の SHA-256 と .md の照合) は各 Stage が従来どおり行う
echo "== build =="
sh tools/build.sh all > tmp/test-buildall.log 2>&1
buildrc=$?
report $buildrc "build: 全 Stage の生成物 (log: tmp/test-buildall.log)"
# 落ちたら記録の末尾をその場に出す。CI では tmp/ が残らないので，
# これが無いと何が起きたか判らない (第 6 部で実際に困った)
if [ "$buildrc" -ne 0 ]; then
    echo "--- tmp/test-buildall.log の末尾 40 行 ---"
    tail -40 tmp/test-buildall.log
    echo "--- ここまで ---"
fi
overall_fail=$fail
export STONE_PREBUILT=1

# ---- テストのスタンプ ----
#
# ビルドと同じ考え方をテストにも入れる。**入力が前回と一致し，前回
# 通っている Stage は飛ばす。** 入力は「その Stage の検査一式
# (tests/<stage>/**)」「共通の tests/lib.sh」「鎖の全ソース (stage*/**)」
# 「生成物のスタンプ (tmp/build/*.stamp)」である。生成物のスタンプには
# すべての成果物の SHA-256 が入っているので，成果物が 1 バイトでも
# 変われば鍵が変わる。
#
# 健全性の根拠はビルドの決定性と同じである (docs/dev-notes.md 1.3)。
#
# 鎖のソースは**その Stage 以下の番号のものだけ**を入れる。当初は
# 絞らず全部入れていたが，それだと**新しい Stage を 1 つ足すだけで
# 全 Stage のキャッシュが外れる** (Stage 16 を足したとき実際に
# 000〜015 が全部走り直した)。各 Stage の検査が参照する stage ディレクトリ
# は自分以下の番号に収まっている (最大は stage013 -> stage009/010/012) ので，
# 後ろの Stage のソースは前の Stage の検査結果を変えようがない。
#
# 前の Stage のソースを外さないのは，検査がソースを直接読む場合がある
# ため (例: stage015 の検査は stage015/libc/*.c をその場で翻訳する)。
# 成果物の側は tmp/build/*.stamp が全世代ぶんの SHA-256 を持っているので，
# 1 バイトでも変われば全 Stage の鍵が変わる。
#
# STONE_FORCE_TEST=1 で無視して全部走らせる。CI の週次はこれを立てる。
mkdir -p tmp/test
teststamp_key() {
    # "stage016" -> 16。この番号以下の stage ディレクトリだけを見る。
    # 10# を付けるのは 008 / 009 を 8 進数と読ませないためである
    _num=$((10#${1#stage}))
    _dirs=()
    for _d in stage[0-9]*; do
        [ -d "$_d" ] || continue
        [ "$((10#${_d#stage}))" -le "$_num" ] && _dirs+=("$_d")
    done
    { find "tests/$1" -type f 2> /dev/null | LC_ALL=C sort | tr '\n' '\0' \
        | xargs -0 sha256sum 2> /dev/null
      sha256sum tests/lib.sh 2> /dev/null
      # 空のときに find を呼ぶと引数なし = カレントディレクトリ全体に
      # なってしまう (stage000 には対応するソースの階層が無い)
      if [ "${#_dirs[@]}" -gt 0 ]; then
          find "${_dirs[@]}" -type f 2> /dev/null | LC_ALL=C sort | tr '\n' '\0' \
              | xargs -0 sha256sum 2> /dev/null
      fi
      cat tmp/build/*.stamp 2> /dev/null
    } | sha256sum | cut -d' ' -f1
}

# 走らせる Stage を並べる
run_list=()
for t in tests/stage*/test.sh; do
    s=$(basename "$(dirname "$t")")
    if [ -n "$targets" ]; then
        case " $targets " in
        *" $s "*) ;;
        *) continue ;;
        esac
    fi
    if [ -z "${STONE_FORCE_TEST:-}" ] \
        && [ "$(cat "tmp/test/$s.stamp" 2> /dev/null)" = "$(teststamp_key "$s")" ]; then
        echo "== tests/$s =="
        echo "cached tests/$s (前回と同じ入力で通っている。STONE_FORCE_TEST=1 で無視)"
        continue
    fi
    run_list+=("$s")
done

if [ -n "${STONE_TEST_SERIAL:-}" ]; then
    for s in "${run_list[@]}"; do
        echo "== tests/$s =="
        bash "tests/$s/test.sh"
        rc=$?
        overall_fail=$((overall_fail + rc))
        [ "$rc" -eq 0 ] && teststamp_key "$s" > "tmp/test/$s.stamp"
    done
else
    maxjobs=${STONE_TEST_JOBS:-$(nproc 2> /dev/null || echo 2)}
    [ "$maxjobs" -lt 1 ] && maxjobs=1
    i=0
    n=${#run_list[@]}
    while [ "$i" -lt "$n" ]; do
        # maxjobs 本を起こし，その組が終わってから次の組へ進む。
        # 組の中は Stage の順に並べてあるので，出力の順序は逐次実行と同じ
        batch_names=()
        batch_pids=()
        j=0
        while [ "$j" -lt "$maxjobs" ] && [ "$i" -lt "$n" ]; do
            s=${run_list[$i]}
            bash "tests/$s/test.sh" > "tmp/test-$s.log" 2>&1 &
            batch_names+=("$s")
            batch_pids+=("$!")
            i=$((i + 1))
            j=$((j + 1))
        done
        for k in "${!batch_pids[@]}"; do
            wait "${batch_pids[$k]}"
            rc=$?
            overall_fail=$((overall_fail + rc))
            echo "== tests/${batch_names[$k]} =="
            cat "tmp/test-${batch_names[$k]}.log"
            # 通った Stage だけスタンプを書く。落ちた Stage は次回も走る
            [ "$rc" -eq 0 ] \
                && teststamp_key "${batch_names[$k]}" \
                    > "tmp/test/${batch_names[$k]}.stamp"
        done
    done
fi

echo "===="
if [ "$overall_fail" -eq 0 ]; then
    echo "result: all passed"
else
    echo "result: $overall_fail test(s) failed"
fi
[ "$overall_fail" -eq 0 ]
