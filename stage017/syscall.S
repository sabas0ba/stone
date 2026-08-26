/* syscall.S --- tcc の世界の syscall スタブ (第 5 部の 2)
 *
 * 我々の OS には 2 つの呼出し規約が同居する (crt1.S の註)。
 * `ld16` の 'E' 前置部が持つ `sys_*` は**我々の規約** (引数はデータ
 * スタック x9 経由) なので，tcc が訳した libc からは呼べない。
 * **標準の RISC-V ABI 版をここに書く。**
 *
 * 番号は RV32 Linux 互換である (docs/stage012-os.md 2.2)。'E' 前置部が
 * 使っているものと同じ番号を使う —— 規約が違うだけで，カーネルとの
 * 約束は 1 つである。
 *
 *   read 63 / write 64 / openat 56 / close 57 / brk 214 / exit 93
 *
 * ## 擬似命令を使わない
 *
 * tcc の RISC-V アセンブラの `li` は**64 ビット向け**で，小さい値でも
 * lui / addi / slli を 8 つ並べる。RV32 でも小さい値なら結果は合うが，
 * 確かめずに頼る理由が無い。`addi rd, zero, imm` 1 つで足りる。
 * `mv` も `addi rd, rs, 0` で書く。
 *
 * `call` は使えない (壊れている。docs/stage017-cc.md 30.3)。ここでは
 * そもそも呼出しをしないので当たらない。
 */

	.globl sys_read
sys_read:
	addi	a7, zero, 63
	ecall
	ret

	.globl sys_write
sys_write:
	addi	a7, zero, 64
	ecall
	ret

	.globl sys_openat
sys_openat:
	addi	a7, zero, 56
	ecall
	ret

	.globl sys_close
sys_close:
	addi	a7, zero, 57
	ecall
	ret

	.globl sys_brk
sys_brk:
	addi	a7, zero, 214
	ecall
	ret

/* sys_ecall(n, a, b, c) --- 番号を引数で受ける汎用スタブ。
 * a0..a3 に n, a, b, c が来るので 1 つずつずらす。
 * **左から順に写す**ので、写す前に元が壊れることはない */
	.globl sys_ecall
sys_ecall:
	addi	a7, a0, 0
	addi	a0, a1, 0
	addi	a1, a2, 0
	addi	a2, a3, 0
	ecall
	ret

/* exit(status) --- 'E' 前置部の exit と同じ意味を持つ。
 *
 * ecall の後ろは**届かない**。念のため回したいところだが，
 * **この翻訳単位の中の名前へは飛べない。** tcc のアセンブラの
 * parse_operand は，記号が VT_EXTERN でも VT_STATIC でもないとき
 * "operand expected" で落とす (riscv64-asm.c 234 行)。同じファイルで
 * 定義した .globl の名前はそのどちらでもないので，`tail exit` は
 * **翻訳が通らない** (docs/stage017-cc.md 31 章に測定を載せた)。
 *
 * そこで `ret` を置く。カーネルが誤って返してきたときだけ効き，
 * 呼び手 (crt1c.c の __start_c なら for(;;)) へ戻る */
	.globl exit
exit:
	addi	a7, zero, 93
	ecall
	ret
