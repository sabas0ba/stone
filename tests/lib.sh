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

# 生成物を用意する。tools/test.sh から通しで走らせる場合は，先に
# tools/build.sh all が一度だけ済ませてあるので作り直さない。
#
# build.sh stageN は stage002 から N までを順に作る。各 Stage のテストが
# 個別にこれを呼ぶと，通し実行ではブートストラップ鎖を Stage の数だけ
# 作り直すことになる (実測で CI の 2 割ほどがこの重複だった)。
# 単体で走らせたときは従来どおりその Stage までをビルドする。
ensure_build() {
    [ -n "${STONE_PREBUILT:-}" ] && return 0
    sh tools/build.sh "$1" > /dev/null 2>&1
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
