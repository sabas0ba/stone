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

# 検査項目を区切る見出し。Stage の中が部に分かれている場合 (Stage 10) は
# その区切りに合わせると，どの部の何が落ちたのかが一覧のまま読める。
# 直前の区切りの結果をその場で締めてから，次の見出しを出す
section() {
    section_close
    echo
    echo "-- $1 --"
    sec_pass=$pass
    sec_fail=$fail
    sec_name=$1
}

# 区切りの結果を 1 行にまとめる。section() と summary() から呼ぶ
section_close() {
    if [ -n "${sec_name:-}" ]; then
        echo "   ($sec_name: $((pass - sec_pass)) 件中 $((fail - sec_fail)) 件失敗)"
    fi
}

# 失敗数を終了コードとして返す。テストスクリプトの末尾で呼ぶ
summary() {
    section_close
    echo
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
