# Stage 3 実行テスト (docs/stage003-asm.md 5 章 4)。
# ラベル・疑似命令・ディレクティブを使い，UART へ "OK" を出力して正常終了する。
start:
  lui x5 0x10000        # UART base
  la x10 msg
loop:
  lbu x11 0(x10)
  beqz x11 done
wait:
  lbu x12 5(x5)         # LSR
  andi x12 x12 0x20     # THRE
  beqz x12 wait
  sb x11 0(x5)
  addi x10 x10 1
  j loop
done:
  li x12 0x5555         # 正常終了
  lui x13 0x100         # test finisher
  sw x12 0(x13)
spin:
  j spin
msg:
  byte 0x4f             # 'O'
  byte 0x4b             # 'K'
  byte 0
.
