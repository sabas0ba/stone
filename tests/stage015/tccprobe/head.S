/* riscv32-tcc に吐かせたものを我々の QEMU で走らせるための入口。
 *
 * QEMU は ELF の entry から始めるが，そこはまだ C の世界ではない。
 * 2 つ立ててから C へ入る。
 *
 *   1. スタックの指し手。RAM は 0x8000_0000 から 128 MiB あるので
 *      8 MiB 上に置く
 *   2. mstatus.FS。RISC-V の浮動小数点は**この 2 bit を立てるまで
 *      使えない** (立てずに fadd などを出すと不正命令になる)。OS が
 *      いれば OS が立てるが，ここは裸なので自分で立てる
 *
 * tcc は自前の RISC-V アセンブラを持つので，これも riscv32-tcc が読む。
 * ただし CSR の名前は解さないので番地で書く (mstatus = 0x300)。 */

.globl _start
_start:
    lui sp, 0x80800
    lui t0, 6            /* FS = 0x6000 (Dirty) */
    csrs 0x300, t0       /* csrs mstatus, t0 */
    jal cmain
1:  j 1b
