# COMMON と weak の定義の試験 (cw2.s と組にする): こちらは weak の定義
    .globl _start
_start:
    ret
    .data
    .weak cw
cw:
    .word 7
