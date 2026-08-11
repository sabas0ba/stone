/* riscv32-tcc に吐かせたものを我々の QEMU で走らせるための入口。
 *
 * QEMU は ELF の entry から始めるが，そこはまだ C の世界ではない。
 * スタックの指し手を立ててから C へ入る。RAM は 0x8000_0000 から
 * 128 MiB あるので，8 MiB 上に置く。
 *
 * tcc は自前の RISC-V アセンブラを持つので，これも riscv32-tcc が読む。 */

.globl _start
_start:
    lui sp, 0x80800
    jal cmain
1:  j 1b
