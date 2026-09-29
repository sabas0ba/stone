#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 追跡ファイルに SPDX 表記があるか、隣接 sidecar で識別できるかを確認する。
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

failed=0
while IFS= read -r -d '' path; do
    case "$path" in
        LICENSE|LICENSES/*|*.license) continue ;;
    esac

    case "$path" in
        stage015/tcc/riscv32.patch) expected=LGPL-2.1-only ;;
        tests/stage017/fixtures/gcc47-defined.c) expected=GPL-3.0-or-later ;;
        *) expected=Apache-2.0 ;;
    esac

    if [ -f "$path.license" ]; then
        if ! grep -Fxq "SPDX-License-Identifier: $expected" "$path.license"; then
            echo "invalid SPDX sidecar: $path.license" >&2
            failed=1
        fi
    elif ! grep -Fq "SPDX-License-Identifier: $expected" < <(head -n 12 "$path"); then
        echo "missing or incorrect SPDX identifier: $path" >&2
        failed=1
    fi
done < <(git ls-files -z)

while IFS= read -r -d '' sidecar; do
    if [ ! -f "${sidecar%.license}" ]; then
        echo "orphan SPDX sidecar: $sidecar" >&2
        failed=1
    fi
done < <(git ls-files -z '*.license')

if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo 'SPDX coverage: ok'
