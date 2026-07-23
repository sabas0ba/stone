#!/bin/bash
# リグレッションテスト (ローカル・CI 共用)。
# 環境の検証を行った後，各 Stage のテスト (tests/stage*/test.sh) を実行する。
# 各 Stage のテスト実体は当該ディレクトリ配下に置き，本スクリプトには追加しない。
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

echo "== env =="
sh tools/env.sh build > tmp/test-build.log 2>&1
report $? "env: イメージビルドと packages.lock の照合 (log: tmp/test-build.log)"
sh tools/env.sh run gdb-multiarch --version > /dev/null 2>&1
report $? "env: gdb-multiarch の導入確認"
overall_fail=$fail

for t in tests/stage*/test.sh; do
    echo "== $(dirname "$t") =="
    bash "$t"
    overall_fail=$((overall_fail + $?))
done

echo "===="
if [ "$overall_fail" -eq 0 ]; then
    echo "result: all passed"
else
    echo "result: $overall_fail test(s) failed"
fi
[ "$overall_fail" -eq 0 ]
