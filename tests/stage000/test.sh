#!/bin/bash
# Stage 0 テスト: hello.bin による実行基盤の検証。
#
# 検証項目:
#   1. hello.bin の完全性 (hello.md 記載の SHA-256 と一致)
#   2. QEMU 通常実行: UART 出力と終了コード
#   3. 実行トレースの記録 (STONE_QEMU_TRACE)
#   4. GDB stub のポート待受け (STONE_QEMU_GDB)
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_root"
. tests/lib.sh
mkdir -p tmp

bin=tests/stage000/hello.bin
doc=tests/stage000/hello.md

# 1. hello.bin の完全性
recorded=$(grep -Eo '^SHA-256: [0-9a-f]{64}' "$doc" | cut -d' ' -f2)
actual=$(sha256sum "$bin")
actual=${actual%% *}
[ -n "$recorded" ] && [ "$actual" = "$recorded" ]
report $? "integrity: hello.bin が hello.md 記載の SHA-256 と一致"

# 2. 通常実行: stdout = 'A', 終了コード 0
sh tools/env.sh qemu "$bin" < /dev/null > tmp/hello.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/hello.out)" = "A" ]
report $? "qemu: UART 出力 'A' と終了コード 0"

# 3. 実行トレース: 終了コード 0，ロードアドレスからの実行が記録されること
rm -f tmp/trace.log
STONE_QEMU_TRACE=tmp/trace.log sh tools/env.sh qemu "$bin" \
    < /dev/null > tmp/hello-trace.out
rc=$?
[ "$rc" -eq 0 ] && [ "$(cat tmp/hello-trace.out)" = "A" ] \
    && grep -q '^0x80000000:' tmp/trace.log
report $? "trace: 実行トレースの記録 (tmp/trace.log)"

# 4. GDB stub: ポート待受け
engine=$(detect_engine)
gdb_name=stone-test-gdb
gdb_port=1234
"$engine" rm -f "$gdb_name" > /dev/null 2>&1
STONE_CONTAINER_NAME=$gdb_name STONE_QEMU_GDB=$gdb_port \
    sh tools/env.sh qemu "$bin" < /dev/null > tmp/hello-gdb.out 2> tmp/hello-gdb.log &
gdb_rc=1
for _ in $(seq 1 40); do
    if (exec 3<> "/dev/tcp/127.0.0.1/$gdb_port") 2> /dev/null; then
        gdb_rc=0
        break
    fi
    sleep 0.5
done
"$engine" rm -f "$gdb_name" > /dev/null 2>&1
wait
report $gdb_rc "gdb: stub のポート待受け (127.0.0.1:$gdb_port)"

summary
