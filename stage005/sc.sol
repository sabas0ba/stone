#/ @file sc.sol
#/ @brief C サブセット言語 sc のコンパイラ (sol 言語で記述)。
#/
#/ 言語仕様は docs/stage005-sc.md 2 章。ビルドは sh tools/build.sh stage005。
#/
#/ @section doxygen コメント規約について
#/ sol の行コメントは '#' から行末までである。Doxygen は '#' 始まりの行を
#/ C 系の文書化コメントとしては解釈しないため，回避策として
#/ 「#/ で始まる行を文書化コメントとする」規約を用いる。タグの語彙は
#/ Doxygen に揃えてあり (@brief / @stack / @note / @return)，Doxygen へ
#/ 掛ける場合は FILTER_PATTERNS で '#/' を '///' へ置換すればよい。
#/ 後継の scc.sc / occ.sc は sc 言語なので '///' がそのまま使える。
#/
#/ @section stack スタック効果の記法
#/ sol はスタック指向言語で，引数も返却値もデータスタックを介して渡す。
#/ 名前付き引数がないため @param は使えない。代わりに Forth 系で慣習的な
#/ スタック効果 ( 引数... -- 結果... ) を @stack として記す。
#/ 左が呼出し前，右が復帰後で，いずれも右端がスタック先頭である。
#/
#/ @section design 構成
#/ 単一パス + 出力バッファ + バックパッチ。構文解析がそのまま命令を吐き，
#/ 前方分岐と前方参照の呼出しは出力バッファへの後埋めで解決する。
#/ 式の値はすべてデータスタック経由の固定テンプレート展開で扱い，
#/ レジスタ割付けは行わない。
#/
#/ @section state 式の解析状態
#/ ety … 直前に解析した式の型
#/ elv … 1 なら左辺値 (スタックにあるのは値ではなくアドレス)
#/ 変数を読んだ直後は elv = 1 とし，値が要る場面で rv を通してロードを出す。
#/ これにより代入の左辺や & の対象で余計なロードを出さずに済む。
#/
#/ @section type 型の符号化
#/ ty = (ポインタ深さ << 16) | 基底   基底 0 = char, 1 = int, 2+k = 構造体 k
#/ この表現なら '*' の付け外しが 65536 の加減算になり，ポインタ判定が
#/ 上位 16 bit を見るだけで済む。

# ---- 領域 (sol の大域領域 0x8010_0000 から確保) ----
#
# sol には構造体がないため，表は「1 エントリ N バイトの生バッファ」として持ち，
# 添字 * N + フィールドオフセット で各項目を読み書きする。
# 各表のレイアウトは下のコメントに記す (@ の後ろがバイトオフセット)。
# 探索で「見つからない」は 0 (アドレス 0 は正当なエントリを指さない) で表す。

buf srcbuf 1048576        # 入力ソース
buf outbuf 1048576        # 生成バイナリ
buf gsym 81920            # 大域記号 40B x 2048: 名前16 種別@16 型@20 値@24 定義@28 配列@32 引数@36
buf lsym 8192             # ローカル記号 32B x 256: 名前16 型@16 オフセット@20 配列@24
buf stab 6144             # 構造体 24B x 256: 名前16 サイズ@16
buf mtab 65536            # メンバ 32B x 2048: sid@0 名前@4..19 型@20 オフセット@24 配列@28
buf sbuf 256              # 文字列リテラル
buf tname 16              # 現トークンの識別子
buf snam 16               # struct 名の退避

var pos  var tok  var tval  var slen
var outp var gcnt var lcnt var scnt var mcnt
var bssp var ety  var elv
var fsz  var cloff var cna var cty var cfe
var mainok var mainoff
var pd  var mk  var mm  var cse
var sspad var ssaddr var biv var bina

# ---- トークン種別 ----
const t_eof 0
const t_num 1
const t_str 2
const t_id 3
const k_int 10
const k_char 11
const k_struct 12
const k_if 13
const k_else 14
const k_while 15
const k_return 16
const o_asn 30
const o_lt 31
const o_gt 32
const o_add 33
const o_sub 34
const o_mul 35
const o_div 36
const o_mod 37
const o_amp 38
const o_or 39
const o_xor 40
const o_not 41
const o_lp 42
const o_rp 43
const o_lb 44
const o_rb 45
const o_lc 46
const o_rc 47
const o_semi 48
const o_comma 49
const o_dot 50
const o_eq 51
const o_ne 52
const o_le 53
const o_ge 54
const o_shl 55
const o_shr 56
const o_aa 57
const o_oo 58
const o_arrow 59

const eot 4

# ---- 生成テンプレート語 ----
const i_pop 0x0004a503    # lw x10 0(x9)
const i_pop2 0x00448493   # addi x9 x9 4
const i_push1 0xffc48493  # addi x9 x9 -4
const i_push2 0x00a4a023  # sw x10 0(x9)
const w_add 0x00a585b3
const w_sub 0x40a585b3
const w_mul 0x02a585b3
const w_div 0x02a5c5b3
const w_mod 0x02a5e5b3
const w_and 0x00a5f5b3
const w_or 0x00a5e5b3
const w_xor 0x00a5c5b3
const w_shl 0x00a595b3
const w_shr 0x00a5d5b3
const w_slt 0x00a5a5b3
const w_sgt 0x00b525b3
const w_x1 0x0015c593     # xori x11 x11 1
const w_eqz 0x0015b593    # sltiu x11 x11 1
const w_nez 0x00b035b3    # sltu x11 x0 x11

# ---- 字句解析 ----
# 1 文字先読みのみで済む単純な字句。結果は tok / tval / tname に入る。

#/ @brief 現在位置の 1 文字を返す (消費しない)。
#/ @stack ( -- c )
fn getch srcbuf pos @ + c@ end
#/ @brief 読取り位置を 1 進める。
#/ @stack ( -- )
fn adv pos @ 1 + pos ! end

#/ @brief 空白か (SP TAB CR LF)。
#/ @stack ( c -- f )
fn isws
  dup 32 = over 9 = | over 13 = | swap 10 = |
end
#/ @brief 10 進数字か。
#/ @stack ( c -- f )
fn isdig
  dup 47 > swap 58 < &
end
#/ @brief 識別子の先頭に置ける文字か (英小文字と _)。大文字は仕様で不可。
#/ @stack ( c -- f )
fn isidh
  dup dup 96 > swap 123 < & swap 95 = |
end
#/ @brief 識別子の 2 文字目以降に置ける文字か。
#/ @stack ( c -- f )
fn isidc
  dup isdig if drop 1 ret then
  isidh
end
#/ @brief 16 進数字か (小文字のみ)。
#/ @stack ( c -- f )
fn ishex
  dup isdig if drop 1 ret then
  dup 96 > swap 103 < &
end
#/ @brief 16 進数字を数値へ。
#/ @stack ( c -- v )
#/ @note 'a' は 97 なので 87 を引くと 10 になる。
fn hexv
  dup 57 > if 87 - ret then
  48 -
end
#/ @brief エスケープ文字を値へ変換する。
#/ @stack ( c -- v )
#/ @note 未対応の文字は構文エラー (終了コード 1) で停止する。
fn escv
  dup 110 = if drop 10 ret then
  dup 116 = if drop 9 ret then
  dup 48 = if drop 0 ret then
  dup 92 = if drop 92 ret then
  dup 39 = if drop 39 ret then
  dup 34 = if drop 34 ret then
  1 exit
end

#/ @brief 空白と行コメント (// 以降) を読み飛ばす。
#/ @stack ( -- )
#/ @note コメント中に EOT が来た場合はそこで打ち切る。打ち切らないと
#/       終端のないコメントで入力の末尾を越えて走り続けてしまう。
fn skipwc                 # 空白と // コメントの読み飛ばし
  begin
    getch
    dup isws if drop adv 1 else
      dup 47 = if
        srcbuf pos @ + 1 + c@ 47 = if
          drop
          begin getch dup 10 <> swap eot <> & while adv repeat
          1
        else drop 0 then
      else drop 0 then
    then
  while repeat
end

#/ @brief 整数リテラルを読み tval へ入れる (10 進 / 0x 16 進)。
#/ @stack ( -- )
fn lexnum
  0 tval !
  getch 48 = srcbuf pos @ + 1 + c@ 120 = & if
    adv adv
    getch ishex not if 1 exit then
    begin getch ishex while
      tval @ 16 * getch hexv + tval !
      adv
    repeat
  else
    begin getch isdig while
      tval @ 10 * getch 48 - + tval !
      adv
    repeat
  then
  t_num tok !
end

#/ @brief 識別子を読んで tname へ格納し，予約語ならその種別を tok に入れる。
#/ @stack ( -- )
#/ @note 予約語は表を持たず，読み終えた後に語全体を 1 語ずつ比較する。
#/       名前は 16 バイト固定スロットなので 4 語の比較で済む。
fn lexid
  0 tname ! 0 tname 4 + ! 0 tname 8 + ! 0 tname 12 + !
  0 tval !
  begin getch isidc while
    tval @ 15 = if 1 exit then
    getch tname tval @ + 1 + c!
    tval @ 1 + tval !
    adv
  repeat
  tval @ tname c!
  tname @ 0x746e6903 = tname 4 + @ 0 = & if k_int tok ! ret then
  tname @ 0x61686304 = tname 4 + @ 0x72 = & if k_char tok ! ret then
  tname @ 0x72747306 = tname 4 + @ 0x746375 = & if k_struct tok ! ret then
  tname @ 0x666902 = tname 4 + @ 0 = & if k_if tok ! ret then
  tname @ 0x736c6504 = tname 4 + @ 0x65 = & if k_else tok ! ret then
  tname @ 0x69687705 = tname 4 + @ 0x656c = & if k_while tok ! ret then
  tname @ 0x74657206 = tname 4 + @ 0x6e7275 = & if k_return tok ! ret then
  t_id tok !
end

#/ @brief 文字リテラルを読み，その文字コードを tval へ入れる。
#/ @stack ( -- )
fn lexchr
  adv
  getch eot = if 1 exit then
  getch 92 = if adv getch escv else getch then tval !
  adv
  getch 39 <> if 1 exit then
  adv
  t_num tok !
end

#/ @brief 文字列リテラルを読み sbuf / slen へ入れる。
#/ @stack ( -- )
#/ @note 末尾に 0 を 4 個書くのは，後段が 4 バイト境界まで切り上げて
#/       出力するため。境界埋めの分まで確実に 0 にしておく。
fn lexstr
  adv
  0 slen !
  begin getch 34 <> while
    getch eot = if 1 exit then
    slen @ 255 = if 6 exit then
    getch 92 = if adv getch escv else getch then
    sbuf slen @ + c!
    adv
    slen @ 1 + slen !
  repeat
  adv
  0 sbuf slen @ + c!
  0 sbuf slen @ 1 + + c!
  0 sbuf slen @ 2 + + c!
  0 sbuf slen @ 3 + + c!
  t_str tok !
end

#/ @brief 演算子・区切り記号を読んで tok に入れる。
#/ @stack ( -- )
#/ @note 2 文字演算子 (== != <= >= << >> && ||) は 1 文字目を消費した後に
#/       次の文字を覗いて分岐する。先に長い方を試すのが要点で，例えば
#/       '<' を見た時点で o_lt を確定してしまうと "<=" が壊れる。
fn lexop
  getch adv
  dup 61 = if drop getch 61 = if adv o_eq else o_asn then tok ! ret then
  dup 33 = if drop getch 61 = if adv o_ne else o_not then tok ! ret then
  dup 60 = if drop
    getch 61 = if adv o_le tok ! ret then
    getch 60 = if adv o_shl tok ! ret then
    o_lt tok ! ret then
  dup 62 = if drop
    getch 61 = if adv o_ge tok ! ret then
    getch 62 = if adv o_shr tok ! ret then
    o_gt tok ! ret then
  dup 38 = if drop
    getch 38 = if adv o_aa tok ! ret then
    o_amp tok ! ret then
  dup 124 = if drop
    getch 124 = if adv o_oo tok ! ret then
    o_or tok ! ret then
  dup 45 = if drop
    getch 62 = if adv o_arrow tok ! ret then
    o_sub tok ! ret then
  dup 43 = if drop o_add tok ! ret then
  dup 42 = if drop o_mul tok ! ret then
  dup 47 = if drop o_div tok ! ret then
  dup 37 = if drop o_mod tok ! ret then
  dup 94 = if drop o_xor tok ! ret then
  dup 40 = if drop o_lp tok ! ret then
  dup 41 = if drop o_rp tok ! ret then
  dup 91 = if drop o_lb tok ! ret then
  dup 93 = if drop o_rb tok ! ret then
  dup 123 = if drop o_lc tok ! ret then
  dup 125 = if drop o_rc tok ! ret then
  dup 59 = if drop o_semi tok ! ret then
  dup 44 = if drop o_comma tok ! ret then
  dup 46 = if drop o_dot tok ! ret then
  drop 1 exit
end

#/ @brief トークンを 1 個読み進める。結果は tok / tval / tname に入る。
#/ @stack ( -- )
fn next
  skipwc
  getch
  dup eot = if drop t_eof tok ! ret then
  dup isdig if drop lexnum ret then
  dup isidh if drop lexid ret then
  dup 39 = if drop lexchr ret then
  dup 34 = if drop lexstr ret then
  drop lexop
end

# ---- 出力バッファ ----
# 生成コードは UART へ直接流さず outbuf へ溜める。前方分岐や前方参照の
# 呼出しを後から書き戻す (backpatch) 必要があり，一度流したものは直せない。

#/ @brief 現在の出力位置 (= 生成コードのオフセット)。
#/ @stack ( -- off )
fn cur outp @ end
#/ @brief 1 語書き，位置を 4 進める。
#/ @stack ( w -- )
fn outw outbuf outp @ + ! outp @ 4 + outp ! end       # ( w -- )
#/ @brief 1 バイト書く (文字列リテラルの実体出力に使う)。
#/ @stack ( b -- )
fn outb outbuf outp @ + c! outp @ 1 + outp ! end      # ( b -- )
#/ @brief 既に書いた位置へ語を上書きする (後埋め)。
#/ @stack ( w off -- )
fn patw outbuf + ! end                                # ( w off -- )
#/ @brief 既に書いた語を読み出す。
#/ @stack ( off -- w )
fn getw outbuf + @ end                                # ( off -- w )

#/ @brief J 形式の即値を命令語のビット位置へ散らす。
#/ @stack ( rel -- w )
#/ @note 配置は imm[20] -> bit31, imm[10:1] -> bit30:21, imm[11] -> bit20,
#/       imm[19:12] -> bit19:12。base とは OR して使う。
fn jenc   # ( rel -- w )      # J-type 即値の合成 (base とは OR する)
  0
  over 20 >> 1 & 31 << |
  over 1 >> 0x3ff & 21 << |
  over 11 >> 1 & 20 << |
  swap 12 >> 0xff & 12 << |
end
#/ @brief B 形式の即値を命令語のビット位置へ散らす。
#/ @stack ( rel -- w )
#/ @note 表現範囲は ±4KiB しかないが，本実装は距離検査をしない。
#/       本体が 4KiB を超える if / while は誤ったアドレスへ飛ぶコードに
#/       なる (Stage 7 で発見・修正。docs/stage007-occ.md 3 章)。
fn benc   # ( rel -- w )      # B-type 即値の合成
  0
  over 12 >> 1 & 31 << |
  over 5 >> 0x3f & 25 << |
  over 1 >> 0xf & 8 << |
  swap 11 >> 1 & 7 << |
end

# ---- 生成ヘルパ (コード生成テンプレート) ----
# 式の値はすべて生成コード側のデータスタック (x9, 下向き) を経由して渡す。
# レジスタ割付けを行わない代わりに，各構文が固定の命令列に展開される。
# x10 / x11 は展開の中でのみ使う作業レジスタである。

#/ @brief データスタックの先頭を x10 へ取り出して捨てる (2 語)。
#/ @stack ( -- )
fn epop i_pop outw i_pop2 outw end
#/ @brief x10 の値をデータスタックへ積む (2 語)。
#/ @stack ( -- )
fn epush i_push1 outw i_push2 outw end
#/ @brief 定数をデータスタックへ積む展開を出す (lui + addi + push の 4 語)。
#/ @stack ( v -- )
#/ @note addi の即値は符号拡張されるため，下位 12 bit の最上位が立つ値では
#/       上位側が 1 足りなくなる。lui へ渡す前に +0x800 して相殺している。
fn elit   # ( v -- )          # リテラル push (lui+addi+push)
  dup 0x800 + 0xfffff000 & 0x537 | outw
  20 << 0x50513 | outw
  epush
end
#/ @brief データスタック上位 2 個を入れ替える (4 語)。
#/ @stack ( -- )
fn eswp                   # スタック上位 2 セルの交換
  0x0004a503 outw 0x0044a583 outw 0x00a4a223 outw 0x00b4a023 outw
end
#/ @brief 二項演算を展開する (上位 2 個を取り出し，演算し，結果を積む)。
#/ @stack ( opw -- )
fn ebin
  0x0004a503 outw 0x0044a583 outw
  outw
  0x00448493 outw 0x00b4a023 outw
end
#/ @brief 2 語を要する二項演算を展開する (比較演算の補正付きなど)。
#/ @stack ( w1 w2 -- )
fn ebin2
  0x0004a503 outw 0x0044a583 outw
  swap outw outw
  0x00448493 outw 0x00b4a023 outw
end

#/ @brief 型が指す実体 1 個の大きさ (バイト)。ポインタ演算のスケール係数。
#/ @stack ( ty -- n )
fn tsize
  dup 0xffff0000 & if drop 4 ret then
  dup 0 = if drop 1 ret then
  dup 1 = if drop 4 ret then
  2 - 24 * stab + 16 + @
end
#/ @brief その型の記憶域へアクセスする幅 (バイト)。1 なら lbu/sb, 4 なら lw/sw。
#/ @stack ( ty -- n )
#/ @note tsize と違い構造体でも 4 を返す。構造体そのものを 1 命令で読み書き
#/       することはなく，スカラのロード/ストア幅の選択にしか使わない。
fn bytesz   # ( ty -- n )     # 記憶域アクセス幅
  dup 0xffff0000 & if drop 4 ret then
  0 = if 1 else 4 then
end

#/ @brief スタック先頭のアドレスを，その指す値で置き換える展開を出す。
#/ @stack ( ty -- )
fn eload   # ( ty -- )        # TOS のアドレスを値に置換
  0x0004a503 outw
  bytesz 1 = if 0x00054503 else 0x00052503 then outw
  0x00a4a023 outw
end
#/ @brief ( アドレス, 値 -- 値 ) の格納を展開する。
#/ @stack ( ty -- )
#/ @note 代入式の値は「格納した値」なので，格納後もスタックに残す。
fn estore   # ( ty -- )       # ( addr val -- val ) の格納を出力
  0x0004a503 outw
  0x0044a583 outw
  bytesz 1 = if 0x00a58023 else 0x00a5a023 then outw
  0x00a4a223 outw
  0x00448493 outw
end
#/ @brief スタック先頭を sz 倍する展開を出す (ポインタ演算のスケーリング)。
#/ @stack ( sz -- )
#/ @note 1 なら何も出さず，4 なら 2 bit シフト，それ以外は乗算に落とす。
fn escale   # ( sz -- )       # TOS *= sz
  dup 1 = if drop ret then
  dup 4 = if drop
    0x0004a503 outw 0x00251513 outw 0x00a4a023 outw ret then
  0x0004a503 outw
  dup 0x800 + 0xfffff000 & 0x5b7 | outw
  20 << 0x58593 | outw
  0x02b50533 outw
  0x00a4a023 outw
end
#/ @brief スタック先頭を sz で割る展開を出す (ポインタ差から要素数を得る)。
#/ @stack ( sz -- )
fn ediv   # ( sz -- )         # TOS /= sz
  dup 1 = if drop ret then
  dup 4 = if drop
    0x0004a503 outw 0x00255513 outw 0x00a4a023 outw ret then
  0x0004a503 outw
  dup 0x800 + 0xfffff000 & 0x5b7 | outw
  20 << 0x58593 | outw
  0x02b54533 outw
  0x00a4a023 outw
end
#/ @brief ローカル変数のアドレスをスタックへ積む展開を出す。
#/ @stack ( off -- )
fn eladdr   # ( off -- )      # ローカルのアドレス push (addi x10 x8 off)
  20 << 0x40513 | outw
  epush
end
#/ @brief スタック先頭のアドレスに定数を加算する (メンバのオフセット加算)。
#/ @stack ( off -- )
#/ @note 0 なら何も出さない。2047 を超える場合は即値に収まらないので
#/       lui + addi で作ってから加算する。
fn eoffs   # ( off -- )       # TOS のアドレスへオフセット加算
  dup 0 = if drop ret then
  dup 2047 > if
    0x0004a503 outw
    dup 0x800 + 0xfffff000 & 0x5b7 | outw
    20 << 0x58593 | outw
    0x00b50533 outw
    0x00a4a023 outw
    ret then
  0x0004a503 outw
  20 << 0x50513 | outw
  0x00a4a023 outw
end
#/ @brief sw x10, off(x8) の命令語を組み立てる。
#/ @stack ( off -- w )
#/ @note S 形式は 12 bit の変位が上位 7 bit と下位 5 bit に分かれて入る。
fn swx8   # ( off -- w )      # sw x10 off(x8)
  dup 5 >> 25 << swap 31 & 7 << | 0x00a42023 |
end
#/ @brief エピローグ (戻り先とフレームポインタの復元・フレーム解放・復帰)。
#/ @stack ( -- )
#/ @note 返却値はデータスタックに積んだ状態で来る。return のたびに出力する
#/       ので 1 つの関数に複数回現れうる。
fn eepilog                # 返却 (結果はデータスタック上)
  0x00012083 outw
  0x00412403 outw
  fsz @ 20 << 0x10113 | outw
  0x00008067 outw
end

# ---- 記号表 ----
# いずれも線形探索。規模 (大域 2048 / ローカル 256) では十分で，
# ハッシュを持つと表の初期化と衝突処理の分だけ実装が増えるため採らない。
# 探索の鍵は常に直近に読んだ識別子 tname である。
# 返すのはエントリの「アドレス」で，0 が見つからなかったことを表す。

#/ @brief tname と一致する大域記号を探す。
#/ @stack ( -- e|0 )
fn gfind
  0
  begin dup gcnt @ < while
    dup 40 * gsym +
    dup @ tname @ = over 4 + @ tname 4 + @ = &
    over 8 + @ tname 8 + @ = & over 12 + @ tname 12 + @ = & if
      swap drop ret then
    drop 1 +
  repeat
  drop 0
end
#/ @brief tname で大域記号を新規登録する (名前のみ設定)。
#/ @stack ( -- e )
#/ @note 表が満杯なら終了コード 6 で停止する。
fn gnew
  gcnt @ 2047 > if 6 exit then
  gcnt @ 40 * gsym +
  gcnt @ 1 + gcnt !
  tname @ over !
  tname 4 + @ over 4 + !
  tname 8 + @ over 8 + !
  tname 12 + @ over 12 + !
end
#/ @brief tname と一致するローカル記号 (引数・ローカル変数) を探す。
#/ @stack ( -- e|0 )
#/ @note ローカルを先に引き，無ければ大域を引く。これが名前の遮蔽になる。
fn lfind
  0
  begin dup lcnt @ < while
    dup 32 * lsym +
    dup @ tname @ = over 4 + @ tname 4 + @ = &
    over 8 + @ tname 8 + @ = & over 12 + @ tname 12 + @ = & if
      swap drop ret then
    drop 1 +
  repeat
  drop 0
end
#/ @brief tname でローカル記号を新規登録する。
#/ @stack ( -- e )
fn lnew
  lcnt @ 255 > if 6 exit then
  lcnt @ 32 * lsym +
  lcnt @ 1 + lcnt !
  tname @ over !
  tname 4 + @ over 4 + !
  tname 8 + @ over 8 + !
  tname 12 + @ over 12 + !
end
#/ @brief tname と一致する構造体を探す。
#/ @stack ( -- e|0 )
fn sfind
  0
  begin dup scnt @ < while
    dup 24 * stab +
    dup @ tname @ = over 4 + @ tname 4 + @ = &
    over 8 + @ tname 8 + @ = & over 12 + @ tname 12 + @ = & if
      swap drop ret then
    drop 1 +
  repeat
  drop 0
end
#/ @brief snam (退避した struct 名) と一致する構造体を探す。
#/ @stack ( -- e|0 )
#/ @note sfind と同じ処理だが鍵が違う。"struct foo bar;" の解析では
#/       tname が既に変数名 bar で上書きされているため，型名は snam から引く。
fn sfind2   # ( -- e|0 )      # snam で構造体を探索 (tname を壊さない)
  0
  begin dup scnt @ < while
    dup 24 * stab +
    dup @ snam @ = over 4 + @ snam 4 + @ = &
    over 8 + @ snam 8 + @ = & over 12 + @ snam 12 + @ = & if
      swap drop ret then
    drop 1 +
  repeat
  drop 0
end
#/ @brief 構造体 k のメンバのうち tname と一致するものを探す。
#/ @stack ( k -- me|0 )
#/ @note メンバは全構造体で 1 本の表に並べ，先頭の sid で所属を絞る。
fn mfind
  mk !
  0
  begin dup mcnt @ < while
    dup 32 * mtab +
    dup @ mk @ = if
      dup 4 + @ tname @ = over 8 + @ tname 4 + @ = &
      over 12 + @ tname 8 + @ = & over 16 + @ tname 12 + @ = & if
        swap drop ret then
    then
    drop 1 +
  repeat
  drop 0
end

#/ @brief 未解決だった前方参照の呼出しを，確定した関数アドレスで埋める。
#/ @stack ( d head -- )
#/ @note 未解決の呼出しは，まだ書けない jal の語そのものに「1 つ前の
#/       未解決位置」を書き込んで数珠つなぎにしてある。別途リスト用の
#/       領域を持たずに済ませるための手である。
fn patchcalls   # ( d head -- )   # 未解決呼出しリストを d へ解決
  swap pd !
  begin dup while
    dup getw swap
    pd @ over - jenc 0xef | swap patw
  repeat
  drop
end
#/ @brief 関数呼出しの jal を出力する。未定義なら未解決リストへ繋ぐ。
#/ @stack ( e -- )
fn ecall
  dup 28 + @ if
    24 + @ cur - jenc 0xef | outw
  else
    cur over 24 + @ outw
    swap 24 + !
  then
end

# ---- 型の解析 ----
# 型の符号化はファイル冒頭の @section type を参照。

#/ @brief 基底型 (int / char / struct 名) を読む。
#/ @stack ( -- base )
#/ @note 型として不正なら終了コード 1，未知の構造体なら 2 で停止する。
fn ptype
  tok @ k_int = if next 1 ret then
  tok @ k_char = if next 0 ret then
  tok @ k_struct = if
    next
    tok @ t_id <> if 1 exit then
    sfind dup 0 = if 2 exit then
    stab - 24 / 2 +
    next ret then
  1 exit
end
#/ @brief 基底型に続く '*' を読み，ポインタ深さを足し込む。
#/ @stack ( base -- ty )
fn pstars
  begin tok @ o_mul = while 0x10000 + next repeat
end

# ---- 式 ----
# 各関数は解析した式のコードを出力し，結果は生成コード側のデータスタックの
# 先頭に残る。補助的な状態 ety / elv の意味はファイル冒頭の @section state。

#/ @brief 左辺値なら値へ変換する (必要ならロードを出力する)。
#/ @stack ( -- )
fn rv
  elv @ if ety @ eload 0 elv ! then
end

#/ @brief 文字列リテラルの実体をコード中に埋め込み，先頭アドレスを積む。
#/ @stack ( -- )
#/ @note 実体は命令列の途中に置くので，実行が流れ込まないよう手前に
#/       跳び越しの jal を出す。長さは 4 バイト境界へ切り上げ，
#/       後続の命令が境界を保つようにする。
fn estr2
  slen @ 4 + 0xfffffffc & sspad !
  sspad @ 4 + jenc 0x6f | outw
  cur 0x80000000 + ssaddr !
  0
  begin dup sspad @ < while
    sbuf over + c@ outb
    1 +
  repeat
  drop
  ssaddr @ elit
  0x10000 ety ! 0 elv !
  next
end

#/ @brief メンバ参照 (. および ->) を解析し，メンバのアドレスを積む。
#/ @stack ( k -- )
#/ @note 呼び出し時点でトークンは '.' か '->' を指している。
#/       配列メンバは先頭要素へのポインタに退化させるので elv = 0 とし，
#/       スカラメンバは左辺値 (elv = 1) のままにしてロードを遅延させる。
fn emember   # ( k -- )       # TOS = 構造体アドレス。'.'/'->' の位置から
  next
  tok @ t_id <> if 1 exit then
  mfind dup 0 = if 5 exit then
  next
  dup 24 + @ eoffs
  dup 28 + @ if
    20 + @ 0x10000 + ety ! 0 elv !
  else
    20 + @ ety ! 1 elv !
  then
end

#/ @brief 関数呼出しの実引数並びを解析し，呼出しを出力する。
#/ @stack ( e -- )
#/ @note 実引数は左から順に評価してデータスタックへ積む。引数個数が
#/       未知 (前方参照) の間は個数検査を省く。
fn ecallseq
  next
  0
  tok @ o_rp <> if
    begin expr rv 1 + tok @ o_comma = while next repeat
  then
  tok @ o_rp <> if 1 exit then
  next
  over 36 + @ dup 0 < if drop drop else <> if 5 exit then then
  dup 20 + @ ety ! 0 elv !
  ecall
end

#/ @brief 識別子を解決する (ローカル -> 大域 -> 未知なら前方参照の関数呼出し)。
#/ @stack ( -- )
#/ @note 未知の名前は「これから定義される関数の呼出し」としてのみ許す。
#/       直後が '(' でなければ未定義識別子 (エラー 2)。仮登録した記号は
#/       未定義のままなので，最後まで定義されなければ main が検出する。
fn eident
  lfind dup if
    dup 24 + @ if
      dup 20 + @ eladdr
      16 + @ 0x10000 + ety ! 0 elv !
    else
      dup 20 + @ eladdr
      16 + @ ety ! 1 elv !
    then
    next ret then
  drop
  gfind dup if
    dup 16 + @ 1 = if
      next
      tok @ o_lp <> if 1 exit then
      ecallseq
      ret then
    dup 32 + @ if
      dup 24 + @ elit
      20 + @ 0x10000 + ety ! 0 elv !
    else
      dup 24 + @ elit
      20 + @ ety ! 1 elv !
    then
    next ret then
  drop
  # 未知: 前方参照の関数呼出しのみ許す
  next
  tok @ o_lp <> if 2 exit then
  gnew
  1 over 16 + !
  1 over 20 + !
  0 over 24 + !
  0 over 28 + !
  0 over 32 + !
  0 1 - over 36 + !
  ecallseq
end

#/ @brief 一次式 (リテラル・識別子・括弧) を解析する。
#/ @stack ( -- )
fn eprim
  tok @ t_num = if tval @ elit 0 elv ! 1 ety ! next ret then
  tok @ t_str = if estr2 ret then
  tok @ o_lp = if
    next expr
    tok @ o_rp <> if 1 exit then
    next ret then
  tok @ t_id = if eident ret then
  1 exit
end

#/ @brief 後置演算 (添字 [] ・メンバ . ・アロー ->) を左から畳み込む。
#/ @stack ( -- )
#/ @note a[i] は *(a + i) と同義に展開する。添字は指し先の大きさで
#/       スケールし，結果は左辺値として返すので代入の左辺にも使える。
fn epost
  eprim
  begin
    tok @ o_lb = if
      rv
      ety @ 0xffff0000 & 0 = if 5 exit then
      ety @
      next expr rv
      tok @ o_rb <> if 1 exit then next
      dup 0x10000 - tsize escale
      w_add ebin
      0x10000 - ety ! 1 elv !
      1
    else tok @ o_dot = if
      elv @ 0 = if 5 exit then
      ety @
      dup 0xffff0000 & if 5 exit then
      dup 2 < if 5 exit then
      2 - emember
      1
    else tok @ o_arrow = if
      rv
      ety @
      dup 0xffff0000 & 0x10000 <> if 5 exit then
      0xffff & dup 2 < if 5 exit then
      2 - emember
      1
    else 0 then then then
  while repeat
end

#/ @brief 単項演算 (- ! * &) を解析する。
#/ @stack ( -- )
#/ @note * と & は elv の付け外しだけで済む。* は「値として得たアドレス」を
#/       左辺値に変える操作，& は「左辺値のアドレス」をそのまま値に変える
#/       操作であり，どちらも命令を生まない。
fn euna
  tok @ o_sub = if next euna rv
    0x0004a503 outw 0x40a00533 outw 0x00a4a023 outw
    1 ety ! ret then
  tok @ o_not = if next euna rv
    0x0004a503 outw 0x00153513 outw 0x00a4a023 outw
    1 ety ! ret then
  tok @ o_mul = if next euna rv
    ety @ 0xffff0000 & 0 = if 5 exit then
    ety @ 0x10000 - ety ! 1 elv ! ret then
  tok @ o_amp = if next euna
    elv @ 0 = if 5 exit then
    0 elv ! ety @ 0x10000 + ety ! ret then
  epost
end

# 以降の二項演算子は優先順位ごとに 1 語を割り当てた再帰下降で，
# 低い優先順位の語が高い方を呼ぶ。いずれも「同じ優先順位が続く限り
# begin/while で左から畳み込む」形なので，自然に左結合になる。

#/ @brief 乗除算 (* / %) を解析する。
#/ @stack ( -- )
fn emul
  euna
  begin tok @ o_mul = tok @ o_div = | tok @ o_mod = | while
    tok @ rv next euna rv
    dup o_mul = if drop w_mul ebin else
    dup o_div = if drop w_div ebin else
    drop w_mod ebin then then
    1 ety ! 0 elv !
  repeat
end

#/ @brief 加減算の型処理 (ポインタ演算のスケーリング)。
#/ @stack ( op lt -- )
#/ @note C と同じ規則を実装する:
#/       ポインタ ± 整数 -> 整数側を要素サイズ倍し，型はポインタのまま
#/       整数 + ポインタ -> 可換なので入れ替えて同様に扱う
#/       ポインタ - ポインタ -> バイト差を要素サイズで割り，型は int
fn edoadd   # ( op lt -- )    # eadd の型処理 (ety = 右辺型)
  swap o_add = if
    dup 0xffff0000 & 0 <>
    ety @ 0xffff0000 & 0 = & if
      dup 0x10000 - tsize escale
      w_add ebin ety ! ret then
    dup 0xffff0000 & 0 =
    ety @ 0xffff0000 & 0 <> & if
      eswp
      ety @ 0x10000 - tsize escale
      eswp
      w_add ebin
      drop ret then
    w_add ebin ety ! ret then
  dup 0xffff0000 & 0 <>
  ety @ 0xffff0000 & 0 = & if
    dup 0x10000 - tsize escale
    w_sub ebin ety ! ret then
  dup 0xffff0000 & 0 <>
  ety @ 0xffff0000 & 0 <> & if
    w_sub ebin
    0x10000 - tsize ediv
    1 ety ! ret then
  w_sub ebin ety !
end

#/ @brief 加減算 (+ -) を解析する。型処理は edoadd に委ねる。
#/ @stack ( -- )
fn eadd
  emul
  begin tok @ o_add = tok @ o_sub = | while
    tok @ rv ety @
    next emul rv
    edoadd
    0 elv !
  repeat
end

#/ @brief シフト (<< >>) を解析する。>> は論理右シフト。
#/ @stack ( -- )
fn eshift
  eadd
  begin tok @ o_shl = tok @ o_shr = | while
    tok @ rv next eadd rv
    o_shl = if w_shl else w_shr then ebin
    1 ety ! 0 elv !
  repeat
end

#/ @brief 大小比較 (< > <= >=) を解析する。結果は 1 / 0。
#/ @stack ( -- )
#/ @note RV32I には slt しかないので，> は左右を入れ替え，
#/       <= と >= は結果を xor 1 で反転して作る。
fn erel
  eshift
  begin tok @ o_lt = tok @ o_gt = | tok @ o_le = | tok @ o_ge = | while
    tok @ rv next eshift rv
    dup o_lt = if drop w_slt ebin else
    dup o_gt = if drop w_sgt ebin else
    dup o_le = if drop w_sgt w_x1 ebin2 else
    drop w_slt w_x1 ebin2 then then then
    1 ety ! 0 elv !
  repeat
end

#/ @brief 等値比較 (== !=) を解析する。結果は 1 / 0。
#/ @stack ( -- )
#/ @note 差を取ってから，== は sltiu ..,1 (差が 0 なら 1)，
#/       != は sltu x0,.. (差が 0 以外なら 1) で 1/0 に落とす。
fn eeq
  erel
  begin tok @ o_eq = tok @ o_ne = | while
    tok @ rv next erel rv
    o_eq = if w_sub w_eqz ebin2 else w_sub w_nez ebin2 then
    1 ety ! 0 elv !
  repeat
end

#/ @brief ビット AND (&) を解析する。
#/ @stack ( -- )
fn eband
  eeq
  begin tok @ o_amp = while
    rv next eeq rv
    w_and ebin
    1 ety ! 0 elv !
  repeat
end

#/ @brief ビット XOR (^) を解析する。
#/ @stack ( -- )
fn exor
  eband
  begin tok @ o_xor = while
    rv next eband rv
    w_xor ebin
    1 ety ! 0 elv !
  repeat
end

#/ @brief ビット OR (|) を解析する。
#/ @stack ( -- )
fn ebor
  exor
  begin tok @ o_or = while
    rv next exor rv
    w_or ebin
    1 ety ! 0 elv !
  repeat
end

#/ @brief 論理 AND (&&) を解析する。左辺が偽なら右辺を評価しない (短絡)。
#/ @stack ( -- )
#/ @note 左辺が 0 なら右辺を飛ばして 0 を積み，そうでなければ右辺を
#/       評価して 1/0 に正規化する。飛び先は前方なので，分岐を先に
#/       出しておき，行き先が確定した時点で後埋めする。
fn ecand
  ebor
  begin tok @ o_aa = while
    rv
    epop
    cur 0x00050063 outw
    next ebor rv
    epop
    0x00a03533 outw
    epush
    cur 0x0000006f outw
    swap
    cur over - benc 0x00050063 | swap patw
    0xffc48493 outw
    0x0004a023 outw
    cur over - jenc 0x6f | swap patw
    1 ety ! 0 elv !
  repeat
end

#/ @brief 論理 OR (||) を解析する。左辺が真なら右辺を評価しない (短絡)。
#/ @stack ( -- )
#/ @note 構造は ecand と対称で，飛び先で積む定数が 1 になる。
fn ecor
  ecand
  begin tok @ o_oo = while
    rv
    epop
    cur 0x00051063 outw
    next ecand rv
    epop
    0x00a03533 outw
    epush
    cur 0x0000006f outw
    swap
    cur over - benc 0x00051063 | swap patw
    0x00100513 outw
    epush
    cur over - jenc 0x6f | swap patw
    1 ety ! 0 elv !
  repeat
end

#/ @brief 式を解析する (代入を含む最上位)。
#/ @stack ( -- )
#/ @note 代入だけは右結合なので begin/while ではなく自分自身を再帰呼出しする。
#/       左辺は左辺値でなければならず，そうでなければ型エラー (5)。
fn expr
  ecor
  tok @ o_asn = if
    elv @ 0 = if 5 exit then
    ety @
    0 elv !
    next expr rv
    dup estore
    ety !
  then
end

# ---- 文 ----
#/ @brief 文を 1 個解析してコードを出力する。
#/ @stack ( -- )
#/ @note 前方への分岐は，行き先が決まる前に命令を出してしまい，位置を
#/       覚えておいて後から書き戻す (backpatch)。while の後方分岐は
#/       行き先が既知なのでその場で確定する。
#/       式文の値は捨てる必要があるので，末尾でスタックを 1 つ縮める。
fn stmt
  tok @ o_lc = if
    next
    begin tok @ o_rc <> while stmt repeat
    next ret then
  tok @ k_if = if
    next tok @ o_lp <> if 1 exit then next
    expr rv
    tok @ o_rp <> if 1 exit then next
    epop
    cur 0x00050063 outw
    stmt
    tok @ k_else = if
      next
      cur 0x0000006f outw
      swap
      cur over - benc 0x00050063 | swap patw
      stmt
      cur over - jenc 0x6f | swap patw
    else
      cur over - benc 0x00050063 | swap patw
    then
    ret then
  tok @ k_while = if
    next tok @ o_lp <> if 1 exit then next
    cur
    expr rv
    tok @ o_rp <> if 1 exit then next
    epop
    cur 0x00050063 outw
    stmt
    swap cur - jenc 0x6f | outw
    cur over - benc 0x00050063 | swap patw
    ret then
  tok @ k_return = if
    next
    tok @ o_semi = if
      0 elit
    else
      expr rv
      tok @ o_semi <> if 1 exit then
    then
    next
    eepilog
    ret then
  tok @ o_semi = if next ret then
  expr
  tok @ o_semi <> if 1 exit then next
  i_pop2 outw
end

# ---- 宣言 ----

#/ @brief tname を snam へ退避する (型名を保持しつつ変数名を読むため)。
#/ @stack ( -- )
fn tnamtos
  tname @ snam ! tname 4 + @ snam 4 + !
  tname 8 + @ snam 8 + ! tname 12 + @ snam 12 + !
end
#/ @brief snam を tname へ戻す。
#/ @stack ( -- )
fn snamtot
  snam @ tname ! snam 4 + @ tname 4 + !
  snam 8 + @ tname 8 + ! snam 12 + @ tname 12 + !
end

#/ @brief 構造体メンバを 1 個解析して登録する。
#/ @stack ( mty -- )
#/ @note オフセットは宣言順に積む。char 配列だけは詰めて置き，それ以外は
#/       4 バイト境界へ揃える。ワード単位のロード/ストアが境界を跨がない
#/       ようにするためである。
fn memb
  dup 0xffff0000 & 0 = over 0xffff & 1 > & if 5 exit then
  tok @ t_id <> if 1 exit then
  cse @ stab - 24 / mfind if 4 exit then
  mcnt @ 2047 > if 6 exit then
  mcnt @ 32 * mtab + mm !
  mcnt @ 1 + mcnt !
  cse @ stab - 24 / mm @ !
  tname @ mm @ 4 + ! tname 4 + @ mm @ 8 + !
  tname 8 + @ mm @ 12 + ! tname 12 + @ mm @ 16 + !
  mm @ 20 + !
  next
  tok @ o_lb = if
    next tok @ t_num <> if 1 exit then
    1 mm @ 28 + !
    mm @ 20 + @ 0 = if
      cse @ 16 + @ mm @ 24 + !
      cse @ 16 + @ tval @ + cse @ 16 + !
    else
      cse @ 16 + @ 3 + 0xfffffffc & dup mm @ 24 + !
      tval @ 4 * + cse @ 16 + !
    then
    next tok @ o_rb <> if 1 exit then next
  else
    0 mm @ 28 + !
    cse @ 16 + @ 3 + 0xfffffffc & dup mm @ 24 + !
    4 + cse @ 16 + !
  then
  tok @ o_semi <> if 1 exit then next
end

#/ @brief 構造体定義 (struct 名 { ... };) を解析して登録する。
#/ @stack ( -- )
#/ @note 先に空の構造体として登録してからメンバを読む。こうするとメンバに
#/       自分自身へのポインタ (連結リストの next など) を書ける。
fn structdef              # snam = 名前, tok = '{'
  snamtot
  sfind if 4 exit then
  scnt @ 255 > if 6 exit then
  scnt @ 24 * stab + cse !
  tname @ cse @ ! tname 4 + @ cse @ 4 + !
  tname 8 + @ cse @ 8 + ! tname 12 + @ cse @ 12 + !
  0 cse @ 16 + !
  scnt @ 1 + scnt !
  next
  begin tok @ o_rc <> while
    ptype pstars memb
  repeat
  next
  tok @ o_semi <> if 1 exit then next
  cse @ 16 + @ 3 + 0xfffffffc & cse @ 16 + !
end

#/ @brief 関数定義を解析し，プロローグ・本体・エピローグを出力する。
#/ @stack ( -- )
#/ @note 既に記号がある場合は前方参照で仮登録されたものなので，関数で
#/       あること・未定義であることを確かめてから引き継ぎ，溜まっていた
#/       未解決の呼出しをこの時点で解決する。
#/       引数はデータスタックに積まれて来るので，プロローグで逆順に
#/       取り出してフレームへ移す。以降は普通のローカル変数として扱える。
#/       本体末尾には無条件に「return 0」相当を足す。return を通らずに
#/       関数の終わりへ到達した場合の返却値を仕様どおり 0 にするため。
fn funcdef                # tname = 名前, tok = '(', cty = 返却型
  gfind dup if
    dup 16 + @ 1 <> if 4 exit then
    dup 28 + @ if 4 exit then
    cfe !
  else
    drop gnew cfe !
    1 cfe @ 16 + !
    0 cfe @ 24 + !
  then
  cty @ cfe @ 20 + !
  cfe @ 24 + @
  cur cfe @ 24 + !
  1 cfe @ 28 + !
  0 cfe @ 32 + !
  cur swap patchcalls
  cfe @ @ 0x69616d04 = cfe @ 4 + @ 0x6e = & if
    cur mainoff ! 1 mainok !
  then
  0 lcnt !
  0 cna !
  next
  tok @ o_rp <> if
    begin
      ptype pstars
      tok @ t_id <> if 1 exit then
      lfind if 4 exit then
      lnew
      swap over 16 + !
      cna @ 4 * 8 + over 20 + !
      0 swap 24 + !
      cna @ 1 + cna !
      next
      tok @ o_comma =
    while next repeat
  then
  tok @ o_rp <> if 1 exit then next
  cna @ cfe @ 36 + !
  tok @ o_lc <> if 1 exit then next
  cna @ 4 * 8 + cloff !
  begin tok @ k_int = tok @ k_char = | tok @ k_struct = | while
    ptype pstars
    dup 0xffff0000 & 0 = over 0xffff & 1 > & if 5 exit then
    tok @ t_id <> if 1 exit then
    lfind if 4 exit then
    lnew
    swap over 16 + !
    cloff @ over 20 + !
    next
    tok @ o_lb = if
      next tok @ t_num <> if 1 exit then
      1 over 24 + !
      dup 16 + @ 0 = if tval @ 3 + 0xfffffffc & else tval @ 4 * then
      cloff @ + cloff !
      next tok @ o_rb <> if 1 exit then next
    else
      0 over 24 + !
      cloff @ 4 + cloff !
    then
    drop
    tok @ o_semi <> if 1 exit then next
  repeat
  cloff @ 2040 > if 6 exit then
  cloff @ fsz !
  0 cloff @ - 0xfff & 20 << 0x10113 | outw
  0x00112023 outw
  0x00812223 outw
  0x00010413 outw
  cna @
  begin dup while
    1 -
    epop
    dup 4 * 8 + swx8 outw
  repeat
  drop
  begin tok @ o_rc <> while stmt repeat
  next
  0 elit
  eepilog
end

#/ @brief 型を読んだ後の宣言を処理する (関数定義か大域変数かをここで分ける)。
#/ @stack ( base -- )
#/ @note 名前の次が '(' なら関数，そうでなければ大域変数。大域変数には
#/       bssp から順にアドレスを与える。実体はバイナリに含めず，実行時の
#/       BSS 領域を指すだけである。
fn dcont
  pstars cty !
  tok @ t_id <> if 1 exit then
  next
  tok @ o_lp = if funcdef ret then
  gfind if 4 exit then
  gnew cfe !
  0 cfe @ 16 + !
  cty @ cfe @ 20 + !
  bssp @ cfe @ 24 + !
  1 cfe @ 28 + !
  0 cfe @ 36 + !
  tok @ o_lb = if
    next tok @ t_num <> if 1 exit then
    1 cfe @ 32 + !
    cty @ 0 = if tval @ 3 + 0xfffffffc & else tval @ 4 * then
    bssp @ + bssp !
    next tok @ o_rb <> if 1 exit then next
  else
    0 cfe @ 32 + !
    cty @ tsize 3 + 0xfffffffc & bssp @ + bssp !
  then
  tok @ o_semi <> if 1 exit then next
end

#/ @brief トップレベルの宣言を 1 個処理する。
#/ @stack ( -- )
#/ @note "struct 名 {" なら定義，"struct 名 名前" なら既存の構造体型を
#/       使った宣言。1 トークン先読みするために型名を snam へ退避する。
fn topdecl
  tok @ k_struct = if
    next
    tok @ t_id <> if 1 exit then
    tnamtos
    next
    tok @ o_lc = if structdef ret then
    sfind2 dup 0 = if 2 exit then
    stab - 24 / 2 +
    dcont ret then
  ptype
  dcont
end

# ---- 組込み関数の登録 ----

#/ @brief 組込み関数を定義済みの大域記号として登録する。
#/ @stack ( w0 w1 val na -- )
#/ @note w0/w1 は名前を 2 語に詰めたもの。sol に文字列リテラルはあるが
#/       ここでは名前スロットへ直接書くほうが短いのでこの形にしている。
fn bi1
  bina ! biv !
  tname 4 + ! tname !
  0 tname 8 + ! 0 tname 12 + !
  gnew
  1 over 16 + !
  1 over 20 + !
  biv @ over 24 + !
  1 over 28 + !
  0 over 32 + !
  bina @ over 36 + !
  drop
end
#/ @brief getc / putc / exit を登録する。
#/ @stack ( -- )
#/ @note アドレスはランタイム前置部の中の固定位置で，普通の関数と同じく
#/       jal で呼べる。前置部は必ず出力の先頭に置くので位置は不変である。
fn bireg
  0x74656704 0x63 0x44 0 bi1
  0x74757004 0x63 0x60 1 bi1
  0x69786504 0x74 0x7c 1 bi1
end

# ---- 駆動部 ----

#/ @brief コンパイラ本体。標準入力からソースを読み，標準出力へバイナリを書く。
#/ @stack ( -- )
#/ @note 段取り:
#/       1. EOT (0x04) までソースを読み切る。UART には EOF がないため
#/          明示的な終端文字を使う
#/       2. ランタイム前置部 32 語を出力する。レジスタ初期化・main 呼出し・
#/          getc/putc/exit を含む。main のアドレスはこの時点で未定なので
#/          5 語目は 0 で埋め，全体を読み終えてから後埋めする
#/       3. トップレベル宣言を順に処理する
#/       4. main の存在と，前方参照のまま定義されなかった関数がないことを検査
#/       5. 溜めたバイナリを 1 バイトずつ書き出す
fn main
  # 入力の読込み (EOT まで)
  0 pos !
  begin getc dup srcbuf pos @ + c! eot <> while
    pos @ 1 + pos !
  repeat
  0 pos !
  0 outp ! 0 gcnt ! 0 scnt ! 0 mcnt ! 0 mainok !
  0x80100000 bssp !
  # ランタイム前置部 (32 語。語 4 = jal x1 main は後で patch)
  0x87f004b7 outw
  0x87800137 outw
  0x100002b7 outw
  0x00100337 outw
  0 outw
  0x0004a503 outw
  0x00448493 outw
  0x00050c63 outw
  0x01051513 outw
  0x000035b7 outw
  0x33358593 outw
  0x00b56533 outw
  0x00c0006f outw
  0x00005537 outw
  0x55550513 outw
  0x00a32023 outw
  0x0000006f outw
  0x0052c503 outw
  0x00157513 outw
  0xfe050ce3 outw
  0x0002c503 outw
  0xffc48493 outw
  0x00a4a023 outw
  0x00008067 outw
  0x0052c583 outw
  0x0205f593 outw
  0xfe058ce3 outw
  0x0004a503 outw
  0x00a28023 outw
  0x0004a023 outw
  0x00008067 outw
  0xf99ff06f outw
  bireg
  next
  begin tok @ t_eof <> while topdecl repeat
  mainok @ 0 = if 3 exit then
  mainoff @ 16 - jenc 0xef | 16 patw
  0
  begin dup gcnt @ < while
    dup 40 * gsym +
    16 + @ 1 =
    over 40 * gsym + 28 + @ 0 = & if 2 exit then
    1 +
  repeat
  drop
  0
  begin dup outp @ < while
    outbuf over + c@ putc
    1 +
  repeat
  drop
  0 exit
end
.
