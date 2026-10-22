# COMMON と weak の定義の試験 (cw1.s と組にする): こちらは COMMON。
# どちらの順で結合しても COMMON (.bss) が勝つ
    .comm cw, 4, 4
