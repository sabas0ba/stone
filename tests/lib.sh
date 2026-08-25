# テストスクリプト共通の補助関数。各テストスクリプトから . (source) で読み込む。

pass=0
fail=0
warn=0

# 「通ったが，通るまでにやり直した」ことを記録する。
#
# QEMU を通す比較の答が**同じ入力・同じ道具でも揺らぐ**ことがある
# (docs/dev-notes.md 1.6)。1 回の取りこぼしで「鎖が壊れた」と報告するのは
# 誤りだが，黙って通すのはもっと悪い。やり直して通ったものは通ったと
# 数え，**summary の 1 行に必ず出す**。
warned() {
    echo "warn $1"
    warn=$((warn + 1))
}

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
    if [ "$warn" -gt 0 ]; then
        echo "passed: $pass, failed: $fail, warned: $warn (やり直して通ったものがある)"
    else
        echo "passed: $pass, failed: $fail"
    fi
    return "$fail"
}

# 生成物を用意する。tools/test.sh から通しで走らせる場合は，先に
# tools/build.sh all が一度だけ済ませてあるので作り直さない。
#
# build.sh stageN は stage002 から N までを順に作る。各 Stage のテストが
# 個別にこれを呼ぶと，通し実行ではブートストラップ鎖を Stage の数だけ
# 作り直すことになる (実測で CI の 2 割ほどがこの重複だった)。
# 単体で走らせたときは従来どおりその Stage までをビルドする。
# ---- 一致の検査: 「中身が違う」と「実行が再現していない」を分ける ----
#
# **同じ入力・同じ道具でも答が揺らぐ環境がある** (docs/dev-notes.md 1.6)。
# ビット一致の検査が落ちたとき，落ちたのが
#
#   * 中身が違う (退行)          -> 直すべきもの。すぐ出す
#   * 実行が再現していない (環境) -> 比較の前提そのものが崩れている
#
# のどちらなのかを**区別して出す**。緑に見せるための再試行ではない ——
# 区別が付かないまま赤にすると，本物の退行を見落とす。逆に，区別が
# 付かないまま黙って通すのはもっと悪い。
#
#   stable_cmp <名前> <生成する関数> <期待するファイル>
#
# <生成する関数> は出力先を 1 つ引数に取り，そこへ作る。**同じものを
# 2 度作らせて自己再現を見る**のが要点である。自己再現するなら
# やり直しても同じなので，その場で退行として出す (やり直しに QEMU を
# 3 回使うのは無駄である。1.6 の元の書き方はここで回していた)。
#
# 作業場は stable_dir に置く。**Stage ごとに分ける** —— 検査は
# 並列に走るので，共有するとお互いの中間結果を踏む
stable_dir=${stable_dir:-tmp/stable}

# QEMU を回して出力を作る手順を，**打ち切られたときだけ**やり直す。
#
#   stable_out <名前> <手順>
#
# <手順> は引数を取らず，QEMU を回して出力を作り，成否を終了状態で返す。
# tools/run-qemu.sh は打ち切ったとき 124 を返すので，**それだけ**を見る。
#
# **答が違うときはやり直さない。** それは中身の話であって環境ではない
# (そちらは stable_cmp が扱う)。ここが見るのは「1 秒で終わるはずのものが
# 900 秒動かない」という，答以前の揺らぎである (docs/dev-notes.md 1.6)。
#
# やり直しは 1 度きり。**本当に止まるものは 2 度目も止まる。**
stable_out() {
    _oname=$1
    _oproc=$2
    _on=0
    while :; do
        "$_oproc"
        _orc=$?
        [ "$_orc" -ne 124 ] && return "$_orc"
        _on=$((_on + 1))
        echo "   $_oname: QEMU が打ち切られた ($_on 回目)"
        if [ "$_on" -ge 2 ]; then
            echo "     2 度とも打ち切られた。環境として片付けず，そのまま落とす"
            return 124
        fi
        echo "     1 秒で終わるはずのものが打ち切りまで動かないのは中身の話では"
        echo "     ない (docs/dev-notes.md 1.6)。もう一度だけ走らせる"
    done
}

stable_cmp() {
    _sname=$1
    _sgen=$2
    _swant=$3
    mkdir -p "$stable_dir"
    _sn=0
    while :; do
        rm -f "$stable_dir/a"
        "$_sgen" "$stable_dir/a"
        _src1=$?
        if [ "$_src1" -eq 0 ] && cmp -s "$stable_dir/a" "$_swant"; then
            return 0
        fi

        # **同じ道具・同じ入力でもう一度。** ここで揃えば環境ではない
        rm -f "$stable_dir/b"
        "$_sgen" "$stable_dir/b" > /dev/null 2>&1
        _src2=$?

        # 3 通りある。**混ぜてはいけない**
        #   生成が落ちた       -> 作れていない。中身の比較以前である
        #   作れたが中身が違う -> 退行
        #   2 度の答が違う     -> 実行が再現していない (環境)
        if [ "$_src1" -ne 0 ]; then
            if [ "$_src2" -ne 0 ]; then _skind=died; else _skind=flaky; fi
        elif cmp -s "$stable_dir/a" "$stable_dir/b"; then
            _skind=differ
        else
            _skind=flaky
        fi

        _sn=$((_sn + 1))
        if [ "$_skind" != flaky ] || [ "$_sn" -ge 3 ]; then
            echo "   $_sname: 生成 rc=$_src1" \
                 "$(stable_id "$stable_dir/a") / 期待 $(stable_id "$_swant")"
            case $_skind in
            died)
                echo "     2 度とも落ちた (rc=$_src1 / rc=$_src2)。" \
                     "**そもそも作れていない**。中身の食い違いではない"
                ;;
            differ)
                echo "     2 度作らせても同じものが出た。**実行は再現している**。"
                echo "     したがってこれは環境ではなく，中身が違う (退行である)"
                ;;
            *)
                echo "     2 度作らせると違うものが出た" \
                     "(rc=$_src2 $(stable_id "$stable_dir/b"))。"
                echo "     **実行が再現していない。比較の前提が崩れている**"
                echo "     (docs/dev-notes.md 1.6)。中身の食い違いとは別の問題である"
                ;;
            esac
            return 1
        fi
        warned "$_sname: $_sn 回目で食い違った (実行が再現していない)。やり直す"
    done
}

# 素性を 1 行で。無いなら無いと言う (黙って空を返さない)
stable_id() {
    if [ -e "$1" ]; then
        echo "($(wc -c < "$1") バイト $(sha256sum "$1" | cut -c1-16))"
    else
        echo "(**無い**)"
    fi
}

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
