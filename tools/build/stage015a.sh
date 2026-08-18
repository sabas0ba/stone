# stage015a のビルド手順と，入力・生成物の宣言。
#
# tools/build.sh から読み込まれる。**このファイル自身がスタンプの入力**
# なので，ここを直せばこの部分だけが作り直される。
#
# ここは第 1〜4 部で封じた cc の世代 (cc15a〜cc15k) だけである。封じた
# 世代の原文はもう変わらないので，普段は常に cached になる。Stage 15 で
# いちばん時間のかかる部分をここへ隔離するのが，この分割の目的である
# (docs/dev-notes.md 1.4)。
#
# 世代ごとに step のスタンプを持つので，途中で殺されても失うのは
# 高々 1 世代である (docs/dev-notes.md 1.5)。

build_stage015a() {
    # 64 bit 整数の土台 (docs/stage015-tcc.md 6 章)。前段は Stage 14 の最前線
    ccgen cc15a cc14g stage015/cc15a.sc
    # 64 bit の演算 (第 2 部の後半)
    ccgen cc15b cc15a stage015/cc15b.sc
    # 64 bit の引数
    ccgen cc15c cc15b stage015/cc15c.sc
    # 64 bit の返却と乗算
    ccgen cc15d cc15c stage015/cc15d.sc
    # 64 bit の除算 (第 2 部の締め)
    ccgen cc15e cc15d stage015/cc15e.sc
    # 第 2 部の穴 3 つの修正 (第 3 部 その 2)
    ccgen cc15f cc15e stage015/cc15f.sc
    # 再配置の表の拡張 (第 3 部の下準備)
    ccgen cc15g cc15f stage015/cc15g.sc
    # float / double の型 (第 3 部)
    ccgen cc15h cc15g stage015/cc15h.sc
    # 浮動小数点の四則と比較 (第 3 部)
    ccgen cc15i cc15h stage015/cc15i.sc
    # 浮動小数点の仮引数と実引数 (第 3 部の締め)
    ccgen cc15j cc15i stage015/cc15j.sc
    # 可変部の 2 語の値 (第 4 部の前提)
    ccgen cc15k cc15j stage015/cc15k.sc
}

do_stage015a() {
    run_stage stage015a cc15a0.bin cc15a.bin cc15b0.bin cc15b.bin \
        cc15c0.bin cc15c.bin cc15d0.bin cc15d.bin \
        cc15e0.bin cc15e.bin cc15f0.bin cc15f.bin \
        cc15g0.bin cc15g.bin cc15h0.bin cc15h.bin \
        cc15i0.bin cc15i.bin cc15j0.bin cc15j.bin \
        cc15k0.bin cc15k.bin \
        -- stage015/cc15a.sc stage015/cc15b.sc stage015/cc15c.sc \
           stage015/cc15d.sc stage015/cc15e.sc stage015/cc15f.sc \
           stage015/cc15g.sc stage015/cc15h.sc stage015/cc15i.sc \
           stage015/cc15j.sc stage015/cc15k.sc \
           tmp/build/stage014.stamp tools/build/stage015a.sh
}
