# テストスクリプト共通の補助関数。各テストスクリプトから . (source) で読み込む。

pass=0
fail=0

report() {
    if [ "$1" -eq 0 ]; then
        echo "ok   $2"
        pass=$((pass + 1))
    else
        echo "FAIL $2"
        fail=$((fail + 1))
    fi
}

# 失敗数を終了コードとして返す。テストスクリプトの末尾で呼ぶ
summary() {
    echo "passed: $pass, failed: $fail"
    return "$fail"
}

detect_engine() {
    if [ -n "${STONE_ENGINE:-}" ]; then
        echo "$STONE_ENGINE"
    elif command -v podman >/dev/null 2>&1; then
        echo podman
    else
        echo docker
    fi
}
