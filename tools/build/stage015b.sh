# stage015b のビルド手順と，入力・生成物の宣言。
#
# 第 6 部で足した最前線の道具 (cc15l〜cc15o・pp15・pp16・ld15・ld16)。
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
    # 容量の世代の pp (マクロ表とアリーナ。12.1)
    tool1 pp15 cc15o stage015/pp15.sc
    # 再帰抑止を直した pp (12.7)
    tool1 pp16 cc15o stage015/pp16.sc
    # tcc の .o (約 1 MB・シンボル数千) を受けるリンカ (12.8)
    tool1 ld15 cc15o stage015/ld15.sc
    # U モードで浮動小数点を使えるようにしたリンカ (12.24)
    tool1 ld16 cc15o stage015/ld16.sc
}

do_stage015b() {
    run_stage stage015b cc15l0.bin cc15l.bin \
        cc15m0.bin cc15m.bin cc15n0.bin cc15n.bin cc15o0.bin cc15o.bin \
        pp15.bin pp16.bin ld15.bin ld16.bin \
        -- stage015/cc15l.sc stage015/cc15m.sc stage015/cc15n.sc \
           stage015/cc15o.sc \
           stage015/pp15.sc stage015/pp16.sc stage015/ld15.sc \
           stage015/ld16.sc \
           tmp/build/stage015a.stamp tools/build/stage015b.sh
}
