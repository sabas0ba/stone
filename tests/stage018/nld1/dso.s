# nld1 の -shared の試験 (dso.lds と組にする)。Linux の vdso と同じ形:
# 位置に依らない 3 つの関数と，VERSION で外に見せない局所の関数
    .text
    .globl dso_alpha
    .type dso_alpha, @function
dso_alpha:
    li a0, 1
    ret
    .size dso_alpha, .-dso_alpha

    .globl dso_beta
    .type dso_beta, @function
dso_beta:
    li a0, 2
    ret
    .size dso_beta, .-dso_beta

    .globl dso_gamma
    .type dso_gamma, @function
dso_gamma:
    call dso_hidden
    ret
    .size dso_gamma, .-dso_gamma

    .globl dso_hidden
    .type dso_hidden, @function
dso_hidden:
    li a0, 3
    ret
    .size dso_hidden, .-dso_hidden
