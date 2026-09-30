# stage004/sol: スタック指向小言語 sol のコンパイラ (asm 言語，docs/stage004-sol.md)
# ビルド: sh tools/build.sh stage004
#
# レジスタ割当は docs/stage004-sol.md 3.5 節を参照。
# link 規約: x1 = ハンドラ->中位，x28 = 中位->emit_word/dup_check，x29 = 下位 (skipws/emit_byte)

# ---- init ----
start:
  lui x5 0x10000          # UART base
  lui x6 0x100            # test finisher base
  lui x18 0x800f0         # scratch (トークン)
  lui x19 0x80100         # 記号表 base
  mv x16 x19              # 記号表 次エントリ
  lui x20 0x80200         # 入力バッファ base

# ---- pass0: UART -> 入力バッファ (コメント/文字列/文字リテラル内の '.' は終端としない) ----
  mv x13 x20
  li x12 0                # 状態: 0=通常 1=コメント 2=文字列
p0_loop:
  lbu x10 5(x5)
  andi x10 x10 1
  beqz x10 p0_loop
  lbu x7 0(x5)
  sb x7 0(x13)
  addi x13 x13 1
  li x10 1
  beq x12 x10 p0_com
  li x10 2
  beq x12 x10 p0_str
  li x10 0x23             # '#'
  beq x7 x10 p0_setcom
  li x10 0x22             # '"'
  beq x7 x10 p0_setstr
  li x10 0x27             # '\''
  beq x7 x10 p0_chr
  li x10 0x2e             # '.'
  beq x7 x10 p0_done
  j p0_loop
p0_com:
  li x10 0x0a
  bne x7 x10 p0_loop
  li x12 0
  j p0_loop
p0_str:
  li x10 0x22
  bne x7 x10 p0_loop
  li x12 0
  j p0_loop
p0_setcom:
  li x12 1
  j p0_loop
p0_setstr:
  li x12 2
  j p0_loop
p0_chr:                   # 文字リテラル: 続く 2 バイトを無条件に転送
  li x11 2
p0_chl:
  lbu x10 5(x5)
  andi x10 x10 1
  beqz x10 p0_chl
  lbu x7 0(x5)
  sb x7 0(x13)
  addi x13 x13 1
  addi x11 x11 -1
  bnez x11 p0_chl
  j p0_loop
p0_done:
  li x15 1                # pass = 1

# ---- pass_init: パス毎の初期化とランタイム前置部 (33 語) の出力 ----
pass_init:
  mv x13 x20              # 入力ポインタ
  lui x14 0x80000         # 出力アドレス
  lui x2 0x801c0          # 制御スタック (上向き)
  li x3 0                 # fixup 連番
  li x4 0                 # 関数内フラグ
  lui x9 0x80100          # 生成プログラムの大域領域アロケータ
  la x24 pre_tmpl
  li x25 0
ep_loop:
  li x10 33
  beq x25 x10 scan_loop
  li x10 4
  bne x25 x10 ep_norm
  li x10 2                # 語 4 (offset 0x10) = jal x1 main
  bne x15 x10 ep_ph
  li x10 0x69616d04       # scratch <- "main"
  sw x10 0(x18)
  li x10 0x6e
  sw x10 4(x18)
  sw x0 8(x18)
  sw x0 12(x18)
  jal x1 sym_lookup
  beqz x17 err3
  lw x22 16(x17)
  li x21 0xef
  jal x1 j_emit
  j ep_next
ep_ph:
  li x21 0x6f
  jal x28 emit_word
  j ep_next
ep_norm:
  lw x21 0(x24)
  jal x28 emit_word
ep_next:
  addi x24 x24 4
  addi x25 x25 1
  j ep_loop

# ---- scanner: pass1/pass2 共用の走査本体 ----
scan_loop:
  jal x29 skipws
  li x10 0x2e             # '.'
  beq x7 x10 s_end
  li x10 0x30
  blt x7 x10 s_nsym
  li x10 0x3a
  blt x7 x10 s_num        # 数字
s_nsym:
  li x10 0x2d             # '-'
  beq x7 x10 s_minus
  li x10 0x27             # '\''
  beq x7 x10 s_char
  li x10 0x22             # '"'
  beq x7 x10 s_str
  j s_wordp
s_minus:
  lbu x10 0(x13)          # 次の文字が数字なら数値
  li x11 0x30
  blt x10 x11 s_wordp
  li x11 0x3a
  blt x10 x11 s_num
  j s_wordp
s_num:
  bnez x4 s_num2
  j err5
s_num2:
  addi x13 x13 -1
  jal x1 parse_num
  jal x1 tok_term
  jal x1 lit_push
  j scan_loop
s_char:
  bnez x4 s_char2
  j err5
s_char2:
  lbu x27 0(x13)          # 値 (任意の 1 バイト)
  lbu x10 1(x13)
  addi x13 x13 2
  li x11 0x27
  beq x10 x11 s_char3
  j err1
s_char3:
  jal x1 lit_push
  j scan_loop
s_str:
  bnez x4 s_str2
  j err5
s_str2:
  lbu x7 0(x13)           # '"' 直後は区切りの SP 1 個
  addi x13 x13 1
  li x10 0x20
  beq x7 x10 s_str3
  j err1
s_str3:
  lui x24 0x800e0         # 文字列組立てバッファ
  li x25 0                # 長さ
s_strl:
  lbu x7 0(x13)
  addi x13 x13 1
  li x10 0x22
  beq x7 x10 s_strf
  add x10 x24 x25
  sb x7 0(x10)
  addi x25 x25 1
  li x10 256
  blt x25 x10 s_strl
  j err1
s_strf:
  add x10 x24 x25         # NUL 終端 + 4 バイト整列まで 0 詰め
  sb x0 0(x10)
  addi x25 x25 1
s_strp:
  andi x10 x25 3
  beqz x10 s_stre
  add x10 x24 x25
  sb x0 0(x10)
  addi x25 x25 1
  j s_strp
s_stre:
  li x10 2                # データを跳び越える jal x0
  beq x15 x10 s_strj
  li x21 0x6f
  jal x28 emit_word
  j s_strd
s_strj:
  addi x22 x14 4
  add x22 x22 x25
  li x21 0x6f
  jal x1 j_emit
s_strd:
  mv x27 x14              # 文字列の先頭アドレス
  li x26 0
s_stred:
  beq x26 x25 s_strq
  add x10 x24 x26
  lbu x8 0(x10)
  jal x29 emit_byte
  addi x26 x26 1
  j s_stred
s_strq:
  jal x1 lit_push
  j scan_loop
s_wordp:
  addi x13 x13 -1
  mv x26 x13              # 参照位置 (入力オフセット)
  jal x1 parse_word
  jal x1 prim_lookup
  beqz x17 s_user
  lw x10 24(x17)          # フラグ (1 = 関数外可)
  bnez x10 s_prdis
  bnez x4 s_prdis
  j err5
s_prdis:
  lw x26 20(x17)          # 引数
  lw x10 16(x17)          # ハンドラ
  jr x10
s_user:
  jal x1 sym_lookup
  beqz x17 s_undef
  lw x10 20(x17)          # 種別 (1=fn 2=var/buf 3=const)
  li x11 1
  beq x10 x11 s_call
  bnez x4 s_vc            # var/const: 値/アドレスを push
  j err5
s_vc:
  li x10 2
  bne x15 x10 s_vc1
  lw x10 24(x17)          # pass2: 前方参照検査 (定義位置と比較)
  bgeu x26 x10 s_vc1
  j err6
s_vc1:
  lw x27 16(x17)
  jal x1 lit_push
  j scan_loop
s_call:
  bnez x4 s_call1
  j err5
s_call1:
  li x10 2
  bne x15 x10 s_callp
  lw x22 16(x17)
  li x21 0xef
  jal x1 j_emit
  j scan_loop
s_callp:
  li x21 0xef
  jal x28 emit_word
  j scan_loop
s_undef:
  bnez x4 s_undef1
  j err5
s_undef1:
  li x10 2
  bne x15 x10 s_undefp
  j err2
s_undefp:
  li x21 0xef             # pass1: 前方参照の関数呼出しとみなす (1 語)
  jal x28 emit_word
  j scan_loop
s_end:
  beqz x4 s_end1
  j err5                  # 関数が未閉包
s_end1:
  li x10 2
  beq x15 x10 exit_ok
  li x15 2
  j pass_init

# ---- skipws: 空白とコメントの読み飛ばし。x7 = 次の有意文字 (消費済み) ----
skipws:
  lbu x7 0(x13)
  addi x13 x13 1
  li x10 0x20
  beq x7 x10 skipws
  li x10 0x09
  beq x7 x10 skipws
  li x10 0x0d
  beq x7 x10 skipws
  li x10 0x0a
  beq x7 x10 skipws
  li x10 0x23
  beq x7 x10 sw_com
  jr x29
sw_com:
  lbu x7 0(x13)
  addi x13 x13 1
  li x10 0x0a
  beq x7 x10 skipws
  j sw_com

# ---- parse_word: トークンを scratch へ (長さ 1 + 名前 15)。終端は ws/'#' (未消費) ----
parse_word:
  sw x0 0(x18)
  sw x0 4(x18)
  sw x0 8(x18)
  sw x0 12(x18)
  li x11 0
pw_loop:
  lbu x7 0(x13)
  li x10 0x21             # '!' 未満 (ws・制御文字) は終端
  blt x7 x10 pw_fin
  li x10 0x23
  beq x7 x10 pw_fin
  li x10 15
  beq x11 x10 pw_ovf
  add x10 x18 x11
  sb x7 1(x10)
  addi x11 x11 1
  addi x13 x13 1
  j pw_loop
pw_ovf:
  j err1
pw_fin:
  bnez x11 pw_fin1
  j err1                  # 長さ 0 (制御文字など)
pw_fin1:
  sb x11 0(x18)
  ret

# ---- parse_num: 数値 ('-'? 10 進 | '-'? '0x' 16 進) -> x27。x13 は先頭文字を指す ----
parse_num:
  li x12 0
  lbu x7 0(x13)
  li x10 0x2d
  bne x7 x10 pn_nos
  li x12 1
  addi x13 x13 1
pn_nos:
  lbu x7 0(x13)
  li x10 0x30
  bne x7 x10 pn_dec
  lbu x7 1(x13)
  li x10 0x78
  bne x7 x10 pn_dec
  addi x13 x13 2
  li x27 0
  li x11 0
pn_hexl:
  lbu x7 0(x13)
  li x10 0x30
  blt x7 x10 pn_hexe
  li x10 0x3a
  blt x7 x10 pn_hx09
  li x10 0x61
  blt x7 x10 pn_hexe
  li x10 0x67
  bge x7 x10 pn_hexe
  addi x10 x7 -0x57
  j pn_hxa
pn_hx09:
  addi x10 x7 -0x30
pn_hxa:
  slli x27 x27 4
  add x27 x27 x10
  addi x13 x13 1
  addi x11 x11 1
  j pn_hexl
pn_hexe:
  bnez x11 pn_fin
  j err1
pn_dec:
  li x27 0
  li x11 0
pn_decl:
  lbu x7 0(x13)
  li x10 0x30
  blt x7 x10 pn_dece
  li x10 0x3a
  bge x7 x10 pn_dece
  slli x10 x27 3
  slli x23 x27 1
  add x27 x10 x23
  addi x10 x7 -0x30
  add x27 x27 x10
  addi x13 x13 1
  addi x11 x11 1
  j pn_decl
pn_dece:
  bnez x11 pn_fin
  j err1
pn_fin:
  beqz x12 pn_ret
  sub x27 x0 x27
pn_ret:
  ret

# ---- tok_term: 数値トークンの終端検査 (ws か '#' でなければ err1) ----
tok_term:
  lbu x10 0(x13)
  li x11 0x20
  beq x10 x11 tt_ok
  li x11 0x09
  beq x10 x11 tt_ok
  li x11 0x0d
  beq x10 x11 tt_ok
  li x11 0x0a
  beq x10 x11 tt_ok
  li x11 0x23
  beq x10 x11 tt_ok
  j err1
tt_ok:
  ret

# ---- prim_lookup: scratch をプリミティブ表から探索。x17 = エントリ or 0 ----
prim_lookup:
  la x17 ptable
pl_loop:
  lbu x10 0(x17)
  beqz x10 pl_nf
  lw x10 0(x17)
  lw x11 0(x18)
  bne x10 x11 pl_next
  lw x10 4(x17)
  lw x11 4(x18)
  bne x10 x11 pl_next
  lw x10 8(x17)
  lw x11 8(x18)
  bne x10 x11 pl_next
  lw x10 12(x17)
  lw x11 12(x18)
  bne x10 x11 pl_next
  ret
pl_nf:
  li x17 0
  ret
pl_next:
  addi x17 x17 28
  j pl_loop

# ---- sym_lookup: scratch を記号表から探索。x17 = エントリ or 0 ----
sym_lookup:
  mv x17 x19
syl_loop:
  bgeu x17 x16 syl_nf
  lw x10 0(x17)
  lw x11 0(x18)
  bne x10 x11 syl_next
  lw x10 4(x17)
  lw x11 4(x18)
  bne x10 x11 syl_next
  lw x10 8(x17)
  lw x11 8(x18)
  bne x10 x11 syl_next
  lw x10 12(x17)
  lw x11 12(x18)
  bne x10 x11 syl_next
  ret
syl_nf:
  li x17 0
  ret
syl_next:
  addi x17 x17 32
  j syl_loop

# ---- sym_add: scratch の名前を登録 (x24=値, x25=種別, x26=定義位置) ----
sym_add:
  lw x10 0(x18)
  sw x10 0(x16)
  lw x10 4(x18)
  sw x10 4(x16)
  lw x10 8(x18)
  sw x10 8(x16)
  lw x10 12(x18)
  sw x10 12(x16)
  sw x24 16(x16)
  sw x25 20(x16)
  sw x26 24(x16)
  sw x0 28(x16)
  addi x16 x16 32
  ret

# ---- dup_check: scratch がプリミティブ・記号表のいずれかにあれば err4 (x28 link) ----
dup_check:
  jal x1 prim_lookup
  beqz x17 dc_1
  j err4
dc_1:
  jal x1 sym_lookup
  beqz x17 dc_2
  j err4
dc_2:
  jr x28

# ---- 制御スタック (8 バイト/段): push (x10=kind, x11=値) / pop -> x10, x11 ----
ctl_push:
  sw x10 0(x2)
  sw x11 4(x2)
  addi x2 x2 8
  ret
ctl_pop:
  lui x10 0x801c0
  bltu x10 x2 cp_ok
  j err5
cp_ok:
  addi x2 x2 -8
  lw x10 0(x2)
  lw x11 4(x2)
  ret

# ---- pop_emit: 条件 pop の 2 語 (lw x10 0(x9); addi x9 x9 4) を出力 ----
pop_emit:
  li x21 0x4a503
  jal x28 emit_word
  li x21 0x448493
  jal x28 emit_word
  ret

# ---- lit_push: x27 を push する 4 語 (lui+addi+push) を出力 ----
lit_push:
  li x10 1
  slli x10 x10 11
  add x10 x27 x10
  lui x23 0xfffff
  and x10 x10 x23
  li x21 0x537            # lui x10 hi
  or x21 x21 x10
  jal x28 emit_word
  slli x10 x27 20
  li x21 0x50513          # addi x10 x10 lo
  or x21 x21 x10
  jal x28 emit_word
  li x21 0xffc48493       # addi x9 x9 -4
  jal x28 emit_word
  li x21 0xa4a023         # sw x10 0(x9)
  jal x28 emit_word
  ret

# ---- j_emit: x21 = base (jal rd), x22 = 絶対目標 -> J 即値を合成して出力 ----
j_emit:
  sub x22 x22 x14
  andi x10 x22 1
  beqz x10 je_1
  j err7
je_1:
  lui x11 0x100
  add x10 x22 x11
  lui x23 0x200
  bltu x10 x23 je_2
  j err7
je_2:
  srli x10 x22 20
  andi x10 x10 1
  slli x10 x10 31
  or x21 x21 x10
  srli x10 x22 1
  andi x10 x10 0x3ff
  slli x10 x10 21
  or x21 x21 x10
  srli x10 x22 11
  andi x10 x10 1
  slli x10 x10 20
  or x21 x21 x10
  srli x10 x22 12
  andi x10 x10 0xff
  slli x10 x10 12
  or x21 x21 x10
  jal x28 emit_word
  ret

# ---- b_emit: x21 = base (beq x10 x0), x22 = 絶対目標 -> B 即値を合成して出力 ----
b_emit:
  sub x22 x22 x14
  andi x10 x22 1
  beqz x10 be_1
  j err7
be_1:
  lui x11 0x1
  add x10 x22 x11
  lui x23 0x2
  bltu x10 x23 be_2
  j err7
be_2:
  srli x10 x22 12
  andi x10 x10 1
  slli x10 x10 31
  or x21 x21 x10
  srli x10 x22 5
  andi x10 x10 0x3f
  slli x10 x10 25
  or x21 x21 x10
  srli x10 x22 1
  andi x10 x10 0xf
  slli x10 x10 8
  or x21 x21 x10
  srli x10 x22 11
  andi x10 x10 1
  slli x10 x10 7
  or x21 x21 x10
  jal x28 emit_word
  ret

# ---- emit_word / emit_byte (出力は pass2 のみ。出力アドレスは常に前進) ----
emit_word:
  andi x8 x21 0xff
  jal x29 emit_byte
  srli x21 x21 8
  andi x8 x21 0xff
  jal x29 emit_byte
  srli x21 x21 8
  andi x8 x21 0xff
  jal x29 emit_byte
  srli x21 x21 8
  andi x8 x21 0xff
  jal x29 emit_byte
  jr x28
emit_byte:
  addi x14 x14 1
  li x10 2
  bne x15 x10 eb_ret
eb_wait:
  lbu x10 5(x5)
  andi x10 x10 0x20
  beqz x10 eb_wait
  sb x8 0(x5)
eb_ret:
  jr x29

# ---- exit / err ----
err1:
  li x10 1
  j do_err
err2:
  li x10 2
  j do_err
err3:
  li x10 3
  j do_err
err4:
  li x10 4
  j do_err
err5:
  li x10 5
  j do_err
err6:
  li x10 6
  j do_err
err7:
  li x10 7
  j do_err
do_err:
  slli x10 x10 16
  lui x11 0x3
  addi x11 x11 0x333
  or x10 x10 x11
  sw x10 0(x6)
  j spin
exit_ok:
  li x10 0x5555
  sw x10 0(x6)
spin:
  j spin

# ---- 定義ハンドラ ----
h_const:
  jal x29 skipws
  addi x13 x13 -1
  mv x26 x13              # 定義位置
  jal x1 parse_word
  jal x29 skipws
  li x10 0x30
  blt x7 x10 h_const_m
  li x10 0x3a
  blt x7 x10 h_const_n
h_const_m:
  li x10 0x2d
  beq x7 x10 h_const_n
  j err1
h_const_n:
  addi x13 x13 -1
  jal x1 parse_num
  jal x1 tok_term
  li x10 2
  beq x15 x10 h_const2
  jal x28 dup_check
  mv x24 x27
  li x25 3
  jal x1 sym_add
h_const2:
  j scan_loop
h_var:
  jal x29 skipws
  addi x13 x13 -1
  mv x26 x13
  jal x1 parse_word
  li x10 2
  beq x15 x10 h_var2
  jal x28 dup_check
  mv x24 x9
  li x25 2
  jal x1 sym_add
h_var2:
  addi x9 x9 4
  j scan_loop
h_buf:
  jal x29 skipws
  addi x13 x13 -1
  mv x26 x13
  jal x1 parse_word
  jal x29 skipws
  li x10 0x30
  blt x7 x10 h_buf_e
  li x10 0x3a
  blt x7 x10 h_buf_n
h_buf_e:
  j err1
h_buf_n:
  addi x13 x13 -1
  jal x1 parse_num
  jal x1 tok_term
  bge x27 x0 h_buf_a
  j err1
h_buf_a:
  addi x27 x27 3          # 4 バイト整列
  li x10 -4
  and x27 x27 x10
  li x10 2
  beq x15 x10 h_buf2
  jal x28 dup_check
  mv x24 x9
  li x25 2
  jal x1 sym_add
h_buf2:
  add x9 x9 x27
  j scan_loop
h_fn:
  beqz x4 h_fn1
  j err5                  # 入れ子定義
h_fn1:
  jal x29 skipws
  addi x13 x13 -1
  mv x26 x13
  jal x1 parse_word
  li x10 2
  beq x15 x10 h_fn2
  jal x28 dup_check
  mv x24 x14              # 関数アドレス = 現在の出力アドレス
  li x25 1
  jal x1 sym_add
h_fn2:
  li x4 1
  li x21 0xffc10113       # addi x2 x2 -4
  jal x28 emit_word
  li x21 0x112023         # sw x1 0(x2)
  jal x28 emit_word
  j scan_loop
h_end:
  bnez x4 h_end1
  j err5
h_end1:
  lui x10 0x801c0
  beq x2 x10 h_end2       # 制御スタックが空であること
  j err5
h_end2:
  li x4 0
h_ret_emit:
  li x21 0x12083          # lw x1 0(x2)
  jal x28 emit_word
  li x21 0x410113         # addi x2 x2 4
  jal x28 emit_word
  li x21 0x8067           # jalr x0 0(x1)
  jal x28 emit_word
  j scan_loop
h_ret:
  bnez x4 h_ret_emit
  j err5

# ---- 制御構造ハンドラ ----
h_if:
  jal x1 pop_emit
  mv x11 x3               # k
  addi x3 x3 1
  li x10 1
  jal x1 ctl_push
  li x10 2
  bne x15 x10 h_if_p1
  lui x10 0x801e0         # pass2: fixup[k] を目標に分岐
  slli x23 x11 2
  add x10 x10 x23
  lw x22 0(x10)
  li x21 0x50063
  jal x1 b_emit
  j scan_loop
h_if_p1:
  li x21 0x50063
  jal x28 emit_word
  j scan_loop
h_else:
  jal x1 ctl_pop
  li x23 1
  beq x10 x23 h_else1
  j err5
h_else1:
  mv x24 x11              # k1 (if の分岐)
  mv x25 x3               # k2 (else の跳び)
  addi x3 x3 1
  li x10 2
  beq x15 x10 h_else2
  lui x10 0x801e0         # pass1: fixup[k1] = else の直後
  slli x23 x24 2
  add x10 x10 x23
  addi x23 x14 4
  sw x23 0(x10)
  li x21 0x6f
  jal x28 emit_word
  j h_else3
h_else2:
  lui x10 0x801e0
  slli x23 x25 2
  add x10 x10 x23
  lw x22 0(x10)
  li x21 0x6f
  jal x1 j_emit
h_else3:
  li x10 1
  mv x11 x25
  jal x1 ctl_push
  j scan_loop
h_then:
  jal x1 ctl_pop
  li x23 1
  beq x10 x23 h_then1
  j err5
h_then1:
  li x10 2
  beq x15 x10 h_then2
  lui x10 0x801e0         # pass1: fixup[k] = 現在地
  slli x23 x11 2
  add x10 x10 x23
  sw x14 0(x10)
h_then2:
  j scan_loop
h_begin:
  li x10 2
  mv x11 x14
  jal x1 ctl_push
  j scan_loop
h_until:
  jal x1 ctl_pop
  li x23 2
  beq x10 x23 h_until1
  j err5
h_until1:
  mv x24 x11              # begin のアドレス
  jal x1 pop_emit
  li x10 2
  bne x15 x10 h_until_p
  mv x22 x24
  li x21 0x50063
  jal x1 b_emit
  j scan_loop
h_until_p:
  li x21 0x50063
  jal x28 emit_word
  j scan_loop
h_while:
  lui x10 0x801c0
  bltu x10 x2 h_while1
  j err5
h_while1:
  lw x10 -8(x2)           # 直下は begin であること
  li x23 2
  beq x10 x23 h_while2
  j err5
h_while2:
  jal x1 pop_emit
  mv x11 x3
  addi x3 x3 1
  li x10 3
  jal x1 ctl_push
  li x10 2
  bne x15 x10 h_while_p
  lui x10 0x801e0
  slli x23 x11 2
  add x10 x10 x23
  lw x22 0(x10)
  li x21 0x50063
  jal x1 b_emit
  j scan_loop
h_while_p:
  li x21 0x50063
  jal x28 emit_word
  j scan_loop
h_repeat:
  jal x1 ctl_pop          # (3, k)
  li x23 3
  beq x10 x23 h_rep1
  j err5
h_rep1:
  mv x24 x11
  jal x1 ctl_pop          # (2, begin)
  li x23 2
  beq x10 x23 h_rep2
  j err5
h_rep2:
  mv x25 x11
  li x10 2
  beq x15 x10 h_rep3
  lui x10 0x801e0         # pass1: fixup[k] = repeat の直後
  slli x23 x24 2
  add x10 x10 x23
  addi x23 x14 4
  sw x23 0(x10)
  li x21 0x6f
  jal x28 emit_word
  j scan_loop
h_rep3:
  mv x22 x25
  li x21 0x6f
  jal x1 j_emit
  j scan_loop

# ---- ランタイム呼出し・テンプレート出力ハンドラ ----
h_rt1:
  li x21 0xef             # jal x1 arg (getc/putc)
  j h_rtc
h_rt0:
  li x21 0x6f             # jal x0 arg (exit)
h_rtc:
  li x10 2
  bne x15 x10 h_rtp
  mv x22 x26
  jal x1 j_emit
  j scan_loop
h_rtp:
  jal x28 emit_word
  j scan_loop
h_tmpl:
  lw x24 0(x26)           # 語数
  addi x26 x26 4
h_tl:
  beqz x24 h_td
  lw x21 0(x26)
  jal x28 emit_word
  addi x26 x26 4
  addi x24 x24 -1
  j h_tl
h_td:
  j scan_loop

# ---- ランタイム前置部テンプレート (33 語。語 4 は jal x1 main に置換される) ----
pre_tmpl:
  word 0x87f004b7         # lui x9 0x87f00      (データスタック)
  word 0x87800137         # lui x2 0x87800      (リターンスタック)
  word 0x100002b7         # lui x5 0x10000      (UART)
  word 0x00100337         # lui x6 0x100        (finisher)
  word 0                  # jal x1 main (pass2 で解決)
  word 0xffc48493         # addi x9 x9 -4       (終了コード 0 を push)
  word 0x0004a023         # sw x0 0(x9)
  word 0x0004a503         # rt_exit: lw x10 0(x9)
  word 0x00448493         # addi x9 x9 4
  word 0x00050c63         # beq x10 x0 +0x18    (0 -> rt_ok)
  word 0x01051513         # slli x10 x10 16
  word 0x000035b7         # lui x11 0x3
  word 0x33358593         # addi x11 x11 0x333
  word 0x00b56533         # or x10 x10 x11
  word 0x00c0006f         # jal x0 +0xc         (-> rt_done)
  word 0x00005537         # rt_ok: lui x10 0x5
  word 0x55550513         # addi x10 x10 0x555
  word 0x00a32023         # rt_done: sw x10 0(x6)
  word 0x0000006f         # jal x0 0            (spin)
  word 0x0052c503         # rt_getc: lbu x10 5(x5)
  word 0x00157513         # andi x10 x10 1
  word 0xfe050ce3         # beq x10 x0 -8
  word 0x0002c503         # lbu x10 0(x5)
  word 0xffc48493         # addi x9 x9 -4
  word 0x00a4a023         # sw x10 0(x9)
  word 0x00008067         # jalr x0 0(x1)
  word 0x0052c583         # rt_putc: lbu x11 5(x5)
  word 0x0205f593         # andi x11 x11 0x20
  word 0xfe058ce3         # beq x11 x0 -8
  word 0x0004a503         # lw x10 0(x9)
  word 0x00448493         # addi x9 x9 4
  word 0x00a28023         # sb x10 0(x5)
  word 0x00008067         # jalr x0 0(x1)

# ---- 展開テンプレート (先頭語 = 語数) ----
t_dup:
  word 3
  word 0x0004a503
  word 0xffc48493
  word 0x00a4a023
t_drop:
  word 1
  word 0x00448493
t_swap:
  word 4
  word 0x0004a503
  word 0x0044a583
  word 0x00a4a223
  word 0x00b4a023
t_over:
  word 3
  word 0x0044a503
  word 0xffc48493
  word 0x00a4a023
t_rot:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x0084a603
  word 0x00c4a023
  word 0x00a4a223
  word 0x00b4a423
t_add:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a585b3
  word 0x00448493
  word 0x00b4a023
t_sub:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x40a585b3
  word 0x00448493
  word 0x00b4a023
t_mul:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x02a585b3
  word 0x00448493
  word 0x00b4a023
t_div:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x02a5c5b3
  word 0x00448493
  word 0x00b4a023
t_rem:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x02a5e5b3
  word 0x00448493
  word 0x00b4a023
t_and:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5f5b3
  word 0x00448493
  word 0x00b4a023
t_or:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5e5b3
  word 0x00448493
  word 0x00b4a023
t_xor:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5c5b3
  word 0x00448493
  word 0x00b4a023
t_sll:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a595b3
  word 0x00448493
  word 0x00b4a023
t_srl:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5d5b3
  word 0x00448493
  word 0x00b4a023
t_lt:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5a5b3
  word 0x00448493
  word 0x00b4a023
t_ult:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00a5b5b3
  word 0x00448493
  word 0x00b4a023
t_gt:
  word 5
  word 0x0004a503
  word 0x0044a583
  word 0x00b525b3
  word 0x00448493
  word 0x00b4a023
t_le:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x00b525b3
  word 0x0015c593
  word 0x00448493
  word 0x00b4a023
t_ge:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x00a5a5b3
  word 0x0015c593
  word 0x00448493
  word 0x00b4a023
t_uge:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x00a5b5b3
  word 0x0015c593
  word 0x00448493
  word 0x00b4a023
t_eq:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x40a585b3
  word 0x0015b593
  word 0x00448493
  word 0x00b4a023
t_ne:
  word 6
  word 0x0004a503
  word 0x0044a583
  word 0x40a585b3
  word 0x00b035b3
  word 0x00448493
  word 0x00b4a023
t_not:
  word 3
  word 0x0004a503
  word 0x00153513
  word 0x00a4a023
t_ld:
  word 3
  word 0x0004a503
  word 0x00052503
  word 0x00a4a023
t_cld:
  word 3
  word 0x0004a503
  word 0x00054503
  word 0x00a4a023
t_st:
  word 4
  word 0x0004a503
  word 0x0044a583
  word 0x00b52023
  word 0x00848493
t_cst:
  word 4
  word 0x0004a503
  word 0x0044a583
  word 0x00b50023
  word 0x00848493

# ---- プリミティブ表 (28 バイト/エントリ: 名前 16 + ハンドラ 4 + 引数 4 + フラグ 4) ----
ptable:
  word 0x6e6f6305         # "const"
  word 0x7473
  word 0
  word 0
  word h_const
  word 0
  word 1
  word 0x72617603         # "var"
  word 0
  word 0
  word 0
  word h_var
  word 0
  word 1
  word 0x66756203         # "buf"
  word 0
  word 0
  word 0
  word h_buf
  word 0
  word 1
  word 0x006e6602         # "fn"
  word 0
  word 0
  word 0
  word h_fn
  word 0
  word 1
  word 0x646e6503         # "end"
  word 0
  word 0
  word 0
  word h_end
  word 0
  word 0
  word 0x74657203         # "ret"
  word 0
  word 0
  word 0
  word h_ret
  word 0
  word 0
  word 0x00666902         # "if"
  word 0
  word 0
  word 0
  word h_if
  word 0
  word 0
  word 0x736c6504         # "else"
  word 0x65
  word 0
  word 0
  word h_else
  word 0
  word 0
  word 0x65687404         # "then"
  word 0x6e
  word 0
  word 0
  word h_then
  word 0
  word 0
  word 0x67656205         # "begin"
  word 0x6e69
  word 0
  word 0
  word h_begin
  word 0
  word 0
  word 0x746e7505         # "until"
  word 0x6c69
  word 0
  word 0
  word h_until
  word 0
  word 0
  word 0x69687705         # "while"
  word 0x656c
  word 0
  word 0
  word h_while
  word 0
  word 0
  word 0x70657206         # "repeat"
  word 0x746165
  word 0
  word 0
  word h_repeat
  word 0
  word 0
  word 0x69786504         # "exit"
  word 0x74
  word 0
  word 0
  word h_rt0
  word 0x8000001c
  word 0
  word 0x74656704         # "getc"
  word 0x63
  word 0
  word 0
  word h_rt1
  word 0x8000004c
  word 0
  word 0x74757004         # "putc"
  word 0x63
  word 0
  word 0
  word h_rt1
  word 0x80000068
  word 0
  word 0x70756403         # "dup"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_dup
  word 0
  word 0x6f726404         # "drop"
  word 0x70
  word 0
  word 0
  word h_tmpl
  word t_drop
  word 0
  word 0x61777304         # "swap"
  word 0x70
  word 0
  word 0
  word h_tmpl
  word t_swap
  word 0
  word 0x65766f04         # "over"
  word 0x72
  word 0
  word 0
  word h_tmpl
  word t_over
  word 0
  word 0x746f7203         # "rot"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_rot
  word 0
  word 0x746f6e03         # "not"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_not
  word 0
  word 0x00002b01         # "+"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_add
  word 0
  word 0x00002d01         # "-"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_sub
  word 0
  word 0x00002a01         # "*"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_mul
  word 0
  word 0x00002f01         # "/"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_div
  word 0
  word 0x00002501         # "%"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_rem
  word 0
  word 0x00002601         # "&"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_and
  word 0
  word 0x00007c01         # "|"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_or
  word 0
  word 0x00005e01         # "^"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_xor
  word 0
  word 0x003c3c02         # "<<"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_sll
  word 0
  word 0x003e3e02         # ">>"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_srl
  word 0
  word 0x00003d01         # "="
  word 0
  word 0
  word 0
  word h_tmpl
  word t_eq
  word 0
  word 0x003e3c02         # "<>"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_ne
  word 0
  word 0x00003c01         # "<"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_lt
  word 0
  word 0x00003e01         # ">"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_gt
  word 0
  word 0x003d3c02         # "<="
  word 0
  word 0
  word 0
  word h_tmpl
  word t_le
  word 0
  word 0x003d3e02         # ">="
  word 0
  word 0
  word 0
  word h_tmpl
  word t_ge
  word 0
  word 0x003c7502         # "u<"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_ult
  word 0
  word 0x3d3e7503         # "u>="
  word 0
  word 0
  word 0
  word h_tmpl
  word t_uge
  word 0
  word 0x00004001         # "@"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_ld
  word 0
  word 0x00002101         # "!"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_st
  word 0
  word 0x00406302         # "c@"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_cld
  word 0
  word 0x00216302         # "c!"
  word 0
  word 0
  word 0
  word h_tmpl
  word t_cst
  word 0
  word 0                  # 表の終端 (長さ 0)
.
