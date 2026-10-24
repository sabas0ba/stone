# a comment line in assembly
#define SYM(n) sym_ ## n
#define SEC(l) .initcall ## l ## .init
	li a0, 'x
SYM(1): .word 0
	.section SEC(0)
