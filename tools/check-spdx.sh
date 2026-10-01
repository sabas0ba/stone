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

# 分析文書の自作部分と上流コードの抜粋を混同しない。
for path in docs/stage015-riscv32.md docs/stage015-tcc.md docs/stage017-cc.md \
            stage015/cc15n.md stage015/cc15o.md \
            docs/stage017-gcc.md stage017/pp18.md; do
    case "$path" in
        docs/stage017-gcc.md|stage017/pp18.md) upstream=GPL-3.0-or-later ;;
        *) upstream=LGPL-2.1-only ;;
    esac
    begin=$(grep -c '^SPDX-SnippetBegin$' "$path" || true)
    end=$(grep -c '^<!-- SPDX-SnippetEnd -->$' "$path" || true)
    labels=$(grep -c "^SPDX-License-Identifier: $upstream$" "$path" || true)
    if [ "$begin" -eq 0 ] || [ "$begin" -ne "$end" ] || [ "$begin" -ne "$labels" ]; then
        echo "invalid upstream SPDX snippets: $path" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo 'SPDX coverage: ok'
