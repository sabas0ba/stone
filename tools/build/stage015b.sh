# stage015b のビルド手順と，入力・生成物の宣言。
#
# 第 6 部で足した最前線の道具 (cc15l〜cc15p・pp15・pp16・ld15・ld16)。
# 前段は stage015a の cc15k である。世代を足すときはここだけが
# 作り直される。
#
# 世代ごとに step のスタンプを持つので，途中で殺されても失うのは
# 高々 1 世代である (docs/dev-notes.md 1.5)。

build_stage015b() {
    # 容量の世代 (第 6 部。tcc の翻訳単位が cc15k の器を溢れさせる)
    ccgen cc15l cc15k stage015/cc15l.sc
    # tcc が使う C の機能 (docs/stage015-tcc.md 12.6)
    ccgen cc15m cc15l stage015/cc15m.sc
    # 整数定数の型 (C89 6.1.3.2。12.14)
    ccgen cc15n cc15m stage015/cc15n.sc
    # 局所の構造体を式で初期化する (12.22)
    ccgen cc15o cc15n stage015/cc15o.sc
    # 構造体の配置を C89 に揃える (14 章)
    ccgen cc15p cc15o stage015/cc15p.sc
    # 第 17 世代。cc15p との差は 2 か所で，どちらも「静的な器の初期化子に
    # 文字列リテラルが出てくると値が壊れる」という 1 つの誤りの別の現れ方
    # (docs/stage017-cc.md 25 章 / 28 章)。
    # **鎖の成果物は変わらない** —— 既存のソースはどちらの形も使っていない。
    # 使うのは tcc の作業場だけ (tools/tcc17.sh)
    ccgen cc15q cc15p stage015/cc15q.sc
    # 第 18 世代。cc15q との差は 1 か所 4 行で，cc15q で直したものと
    # **同じ根** —— 文字列リテラルが配列であることを型として持って
    # いなかった。cc15q は初期化子の側だけを直し，式の側 (sizeof と
    # 単項 &) を見ていなかった (docs/stage017-cc.md 31 章)。
    # **鎖の成果物は変わらない** —— 既存のソースは文字列リテラルの
    # sizeof を使っていない
    ccgen cc15r cc15q stage015/cc15r.sc
    # 第 19 世代。文字列リテラルの族の**最後の 1 つ** ——
    # 多次元の char 配列を文字列で初期化する形 (台帳の staticstr2)。
    # 畳むかどうかを「下位の初期化子が入れ子か」で決める
    # (docs/stage017-cc.md 33 章)
    ccgen cc15s cc15r stage015/cc15s.sc
    # 第 20 世代。宣言指定子の列の途中に来る型修飾子 (unsigned const char)。
    # 先頭の const だけを読んでいたので，整数型指定子の環が const で
    # 抜けてしまっていた。zlib を我々の器で訳して出た穴
    # (docs/stage017-gcc.md 5.1)
    ccgen cc15t cc15s stage015/cc15t.sc
    # 第 21 世代。複合代入が符号を見ていなかった —— a op= b は
    # a = a op b と同じ意味なのに，除算・剰余・右シフトの命令を
    # トークンの並び (符号つき側) のまま選んでいた。zlib の adler32 が
    # 誤った値を返す形で表に出た (docs/stage017-gcc.md 5.1)。
    # **鎖の成果物は変わらない** —— 我々のソースは >>= / /= / %= を
    # 1 つも使っていない。だから自分自身を組む限り表に出ない誤りだった
    ccgen cc15u cc15t stage015/cc15u.sc
    # 第 22 世代。スカラの初期化子を波括弧で囲んだときに中身が 2 つ以上
    # あるのは制約違反 (C89 6.5.7) なのに，黙って隣の要素へ溢れさせて
    # いた。差分試験 (tools/diff17.sh) がホストとの食い違いで見つけた。
    # **鎖の成果物は変わらない** —— 既存のソースにこの形は無い
    ccgen cc15v cc15u stage015/cc15v.sc
    # 容量の世代の pp (マクロ表とアリーナ。12.1)
    tool1 pp15 cc15p stage015/pp15.sc
    # 再帰抑止を直した pp (12.7)
    tool1 pp16 cc15p stage015/pp16.sc
    # tcc の .o (約 1 MB・シンボル数千) を受けるリンカ (12.8)
    tool1 ld15 cc15p stage015/ld15.sc
    # U モードで浮動小数点を使えるようにしたリンカ (12.24)
    tool1 ld16 cc15p stage015/ld16.sc
    # 未定義シンボルの**名前を言う**リンカ (docs/stage017-gcc.md 5.1)。
    # ld16 は exit(2) で拒むだけなので、呼ぶ側に出るのは
    # "cc: link failed" だけだった。通る道のバイト列は変わらない
    tool1 ld17 cc15p stage015/ld17.sc
}

do_stage015b() {
    run_stage stage015b cc15l0.bin cc15l.bin \
        cc15m0.bin cc15m.bin cc15n0.bin cc15n.bin cc15o0.bin cc15o.bin \
        cc15p0.bin cc15p.bin cc15q0.bin cc15q.bin \
        cc15r0.bin cc15r.bin cc15s0.bin cc15s.bin \
        cc15t0.bin cc15t.bin cc15u0.bin cc15u.bin \
        cc15v0.bin cc15v.bin \
        pp15.bin pp16.bin ld15.bin ld16.bin ld17.bin \
        -- stage015/cc15l.sc stage015/cc15m.sc stage015/cc15n.sc \
           stage015/cc15o.sc stage015/cc15p.sc stage015/cc15q.sc \
           stage015/cc15r.sc stage015/cc15s.sc stage015/cc15t.sc \
           stage015/cc15u.sc stage015/cc15v.sc \
           stage015/pp15.sc stage015/pp16.sc stage015/ld15.sc \
           stage015/ld16.sc stage015/ld17.sc \
           tmp/build/stage015a.stamp tools/build/stage015b.sh
}
