/* crt1.S --- tcc が結合する実行形式の入口 (第 5 部の 1)
 *
 * 我々の OS には 2 つの呼出し規約が同居する。
 *
 *   我々の cc (cc15q)  引数はデータスタック (x9) 経由 (stage005-sc.md 145 行)
 *   tcc                標準の RISC-V ABI (a0..a7)
 *
 * ld16 の 'E' 前置部は前者のための入口を機械語で持っている
 * (stage015/ld16.sc prologuee)。tcc が結合する像にはそれが載らないので，
 * **後者のための入口をここに書く**。
 *
 * カーネルが与えるものは 'E' 前置部が読んでいるものと同じである ——
 * argc が [sp]，argv が sp+4 (stage017/kernel24.c)。
 *
 * ## なぜ main を直に call しないのか
 *
 * **tcc の RISC-V アセンブラの `call` が壊れている。**
 * riscv64-asm.c の TOK_ASM_call は
 *
 *     auipc ra, 0
 *     jalr  zero, 0(ra)      <- rd が zero
 *
 * を出す。戻り番地が ra に入らないので，呼ばれた側の `ret` は auipc へ
 * 戻り，**無限に回る**。`tail` と同じものが `call` の名前で出ている
 * (docs/stage017-cc.md 30 章に測定を載せた)。上流の誤りで，我々の
 * riscv32.patch はこの箇所に触れていない。
 *
 * そこでここでは **`tail` で C へ渡すだけ**にし，main の呼出しは
 * C の側 (crt1c.c) に任せる。tcc の**翻訳器**が出す呼出しは正しい
 * (tcc 自身がそれで動いている)。壊れているのはアセンブラの擬似命令
 * だけである。
 */
	.globl _start
_start:
	lw	a0, 0(sp)
	addi	a1, sp, 4
	tail	__start_c
