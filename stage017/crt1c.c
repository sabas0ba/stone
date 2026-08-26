/* crt1c.c --- crt1.S の続き。main を呼び，返り値で終わる (第 5 部の 1)
 *
 * crt1.S が argc / argv を a0 / a1 に置いて tail で飛んでくる。
 * ここから先は普通の C なので，main の呼出しは tcc の翻訳器が出す
 * (アセンブラの `call` は壊れているが，翻訳器の呼出しは正しい。
 * crt1.S の註を見よ)。
 *
 * 終わり方は ld16 の 'E' 前置部と同じ —— a7 に 93 (exit) を置いて
 * ecall する。我々のカーネルの syscall は RV32 Linux 互換なので，
 * この像は実 Linux の上でも同じ意味を持つ (docs/stage012-os.md 2.2)。
 */

int main(int argc, char **argv);

void __start_c(int argc, char **argv) {
  int r;
  r = main(argc, argv);
  /* exit(r)。**戻らない。** 戻り先が無いので，ecall の後ろは
     念のため回しておく (ここへ来たらカーネルの側が誤っている) */
  __asm__ volatile ("mv a0, %0\n\t"
                    "li a7, 93\n\t"
                    "ecall"
                    : : "r" (r) : "a0", "a7");
  for (;;) {
  }
}
