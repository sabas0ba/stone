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
# 各 Stage のテストは並列に走らせる (共有するのは tmp/build の生成物の
# 読取りだけで，書き込み先は Stage ごとに分かれている)。出力は Stage ごとに
# tmp/test-<stage>.log へ取り，Stage の順に完了を待って表示するので，
# 一覧の見た目は逐次実行と変わらない。STONE_TEST_SERIAL=1 で従来どおり
# 1 つずつ実行する (出力がその場で流れるので，単体の追い込みに向く)。
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
report $? "build: 全 Stage の生成物 (log: tmp/test-buildall.log)"
overall_fail=$fail
export STONE_PREBUILT=1

run_names=()
run_pids=()
for t in tests/stage*/test.sh; do
    s=$(basename "$(dirname "$t")")
    if [ -n "$targets" ]; then
        case " $targets " in
        *" $s "*) ;;
        *) continue ;;
        esac
    fi
    if [ -n "${STONE_TEST_SERIAL:-}" ]; then
        echo "== tests/$s =="
        bash "$t"
        overall_fail=$((overall_fail + $?))
        continue
    fi
    bash "$t" > "tmp/test-$s.log" 2>&1 &
    run_names+=("$s")
    run_pids+=("$!")
done
for i in "${!run_pids[@]}"; do
    wait "${run_pids[$i]}"
    overall_fail=$((overall_fail + $?))
    echo "== tests/${run_names[$i]} =="
    cat "tmp/test-${run_names[$i]}.log"
done

echo "===="
if [ "$overall_fail" -eq 0 ]; then
    echo "result: all passed"
else
    echo "result: $overall_fail test(s) failed"
fi
[ "$overall_fail" -eq 0 ]
