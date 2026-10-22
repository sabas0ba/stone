# init1s.s --- init の入口 (docs/stage018-ncc.md 17 章)
#
# Linux の ELF の読込み (binfmt_elf) は sp に argc・argv・envp を置いて
# _start から走らせる。main の戻り値で exit (93) する。init が終わると
# カーネルは panic するので，正常な経路は main の中の reboot である。

    .section .text.init,"ax"
    .globl _start
_start:
    call main
    li a7, 93
    ecall
1:  j 1b
