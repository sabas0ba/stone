# stage005/sc: C サブセット sc のコンパイラ (sol 言語，docs/stage005-sc.md)
# ビルド: sh tools/build.sh stage005
#
# 単一パス + 出力バッファ + バックパッチ構成。
# 式の評価状態は ety (型) と elv (1 = 左辺値: アドレスが積まれロード遅延) で表す。
# 型 ty = (ポインタ深さ << 16) | 基底 (0=char, 1=int, 2+k=構造体 k)。

# ---- 領域 (sol の大域領域 0x8010_0000 から確保) ----
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
fn getch srcbuf pos @ + c@ end
fn adv pos @ 1 + pos ! end

fn isws   # ( c -- f )
  dup 32 = over 9 = | over 13 = | swap 10 = |
end
fn isdig   # ( c -- f )
  dup 47 > swap 58 < &
end
fn isidh   # ( c -- f )
  dup dup 96 > swap 123 < & swap 95 = |
end
fn isidc   # ( c -- f )
  dup isdig if drop 1 ret then
  isidh
end
fn ishex   # ( c -- f )
  dup isdig if drop 1 ret then
  dup 96 > swap 103 < &
end
fn hexv   # ( c -- v )
  dup 57 > if 87 - ret then
  48 -
end
fn escv   # ( c -- v )
  dup 110 = if drop 10 ret then
  dup 116 = if drop 9 ret then
  dup 48 = if drop 0 ret then
  dup 92 = if drop 92 ret then
  dup 39 = if drop 39 ret then
  dup 34 = if drop 34 ret then
  1 exit
end

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

fn lexchr
  adv
  getch eot = if 1 exit then
  getch 92 = if adv getch escv else getch then tval !
  adv
  getch 39 <> if 1 exit then
  adv
  t_num tok !
end

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
fn cur outp @ end
fn outw outbuf outp @ + ! outp @ 4 + outp ! end       # ( w -- )
fn outb outbuf outp @ + c! outp @ 1 + outp ! end      # ( b -- )
fn patw outbuf + ! end                                # ( w off -- )
fn getw outbuf + @ end                                # ( off -- w )

fn jenc   # ( rel -- w )      # J-type 即値の合成 (base とは OR する)
  0
  over 20 >> 1 & 31 << |
  over 1 >> 0x3ff & 21 << |
  over 11 >> 1 & 20 << |
  swap 12 >> 0xff & 12 << |
end
fn benc   # ( rel -- w )      # B-type 即値の合成
  0
  over 12 >> 1 & 31 << |
  over 5 >> 0x3f & 25 << |
  over 1 >> 0xf & 8 << |
  swap 11 >> 1 & 7 << |
end

# ---- 生成ヘルパ ----
fn epop i_pop outw i_pop2 outw end
fn epush i_push1 outw i_push2 outw end
fn elit   # ( v -- )          # リテラル push (lui+addi+push)
  dup 0x800 + 0xfffff000 & 0x537 | outw
  20 << 0x50513 | outw
  epush
end
fn eswp                   # スタック上位 2 セルの交換
  0x0004a503 outw 0x0044a583 outw 0x00a4a223 outw 0x00b4a023 outw
end
fn ebin   # ( opw -- )
  0x0004a503 outw 0x0044a583 outw
  outw
  0x00448493 outw 0x00b4a023 outw
end
fn ebin2   # ( w1 w2 -- )
  0x0004a503 outw 0x0044a583 outw
  swap outw outw
  0x00448493 outw 0x00b4a023 outw
end

fn tsize   # ( ty -- n )
  dup 0xffff0000 & if drop 4 ret then
  dup 0 = if drop 1 ret then
  dup 1 = if drop 4 ret then
  2 - 24 * stab + 16 + @
end
fn bytesz   # ( ty -- n )     # 記憶域アクセス幅
  dup 0xffff0000 & if drop 4 ret then
  0 = if 1 else 4 then
end

fn eload   # ( ty -- )        # TOS のアドレスを値に置換
  0x0004a503 outw
  bytesz 1 = if 0x00054503 else 0x00052503 then outw
  0x00a4a023 outw
end
fn estore   # ( ty -- )       # ( addr val -- val ) の格納を出力
  0x0004a503 outw
  0x0044a583 outw
  bytesz 1 = if 0x00a58023 else 0x00a5a023 then outw
  0x00a4a223 outw
  0x00448493 outw
end
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
fn eladdr   # ( off -- )      # ローカルのアドレス push (addi x10 x8 off)
  20 << 0x40513 | outw
  epush
end
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
fn swx8   # ( off -- w )      # sw x10 off(x8)
  dup 5 >> 25 << swap 31 & 7 << | 0x00a42023 |
end
fn eepilog                # 返却 (結果はデータスタック上)
  0x00012083 outw
  0x00412403 outw
  fsz @ 20 << 0x10113 | outw
  0x00008067 outw
end

# ---- 記号表 ----
fn gfind   # ( -- e|0 )
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
fn gnew   # ( -- e )
  gcnt @ 2047 > if 6 exit then
  gcnt @ 40 * gsym +
  gcnt @ 1 + gcnt !
  tname @ over !
  tname 4 + @ over 4 + !
  tname 8 + @ over 8 + !
  tname 12 + @ over 12 + !
end
fn lfind   # ( -- e|0 )
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
fn lnew   # ( -- e )
  lcnt @ 255 > if 6 exit then
  lcnt @ 32 * lsym +
  lcnt @ 1 + lcnt !
  tname @ over !
  tname 4 + @ over 4 + !
  tname 8 + @ over 8 + !
  tname 12 + @ over 12 + !
end
fn sfind   # ( -- e|0 )
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
fn mfind   # ( k -- me|0 )
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

fn patchcalls   # ( d head -- )   # 未解決呼出しリストを d へ解決
  swap pd !
  begin dup while
    dup getw swap
    pd @ over - jenc 0xef | swap patw
  repeat
  drop
end
fn ecall   # ( e -- )
  dup 28 + @ if
    24 + @ cur - jenc 0xef | outw
  else
    cur over 24 + @ outw
    swap 24 + !
  then
end

# ---- 型の解析 ----
fn ptype   # ( -- base )
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
fn pstars   # ( base -- ty )
  begin tok @ o_mul = while 0x10000 + next repeat
end

# ---- 式 ----
fn rv
  elv @ if ety @ eload 0 elv ! then
end

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

fn ecallseq   # ( e -- )
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

fn eadd
  emul
  begin tok @ o_add = tok @ o_sub = | while
    tok @ rv ety @
    next emul rv
    edoadd
    0 elv !
  repeat
end

fn eshift
  eadd
  begin tok @ o_shl = tok @ o_shr = | while
    tok @ rv next eadd rv
    o_shl = if w_shl else w_shr then ebin
    1 ety ! 0 elv !
  repeat
end

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

fn eeq
  erel
  begin tok @ o_eq = tok @ o_ne = | while
    tok @ rv next erel rv
    o_eq = if w_sub w_eqz ebin2 else w_sub w_nez ebin2 then
    1 ety ! 0 elv !
  repeat
end

fn eband
  eeq
  begin tok @ o_amp = while
    rv next eeq rv
    w_and ebin
    1 ety ! 0 elv !
  repeat
end

fn exor
  eband
  begin tok @ o_xor = while
    rv next eband rv
    w_xor ebin
    1 ety ! 0 elv !
  repeat
end

fn ebor
  exor
  begin tok @ o_or = while
    rv next exor rv
    w_or ebin
    1 ety ! 0 elv !
  repeat
end

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
fn tnamtos
  tname @ snam ! tname 4 + @ snam 4 + !
  tname 8 + @ snam 8 + ! tname 12 + @ snam 12 + !
end
fn snamtot
  snam @ tname ! snam 4 + @ tname 4 + !
  snam 8 + @ tname 8 + ! snam 12 + @ tname 12 + !
end

fn memb   # ( mty -- )
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

fn dcont   # ( base -- )
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
fn bi1   # ( w0 w1 val na -- )
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
fn bireg
  0x74656704 0x63 0x44 0 bi1
  0x74757004 0x63 0x60 1 bi1
  0x69786504 0x74 0x7c 1 bi1
end

# ---- 駆動部 ----
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
