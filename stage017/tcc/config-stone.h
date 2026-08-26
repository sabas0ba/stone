/* config-stone.h --- stone 上で tcc を組むときの config.h (第 2 世代)
 *
 * 上流の configure はホストでしか動かないので，stone 用の設定を
 * 手で固定する。束ねでは "config.h" の名前で載せる
 * (tools/bundle.sh の 名前=パス の形)。
 *
 * stage015/tcc/config-stone.h との差は逆進 (backtrace) と境界検査を
 * 切ったことだけ。詳しくは docs/stage017-cc.md 28.6 と 29 章。
 *
 * - 対象は riscv32 (riscv32.patch の DEF-riscv32 と同じ 2 つの定義)
 * - CONFIG_TCC_STATIC: dlfcn.h が無い (動的ロードは扱わない)
 * - CONFIG_TCCDIR: sfs はルート直下しか無い
 */
#define TCC_VERSION "0.9.28rc"

#define TCC_TARGET_RISCV64 1
#define TCC_TARGET_RISCV32 1

#define CONFIG_TCC_STATIC 1
#define CONFIG_TCCDIR "/"

/* 単一プロセスなので鍵は要らない (semaphore.h も無い) */
#define CONFIG_TCC_SEMLOCK 0

/* 逆進 (backtrace) と境界検査を切る。
 *
 * **上流の tcc から見て，我々の対象では作れないものである。**
 * tcc.h の TCC_IS_NATIVE の条件は
 *
 *   defined __riscv && defined __LP64__ && defined TCC_TARGET_RISCV64
 *
 * で riscv は 64 bit しか見ていない。RV32 の我々では立たないので，
 * tccrun.c の rt_context 以下は最初から無いことになる。ところが
 * lib/Makefile は交叉の接頭辞 X を置いていない我々を native とみなして
 * bt-exe.o を作らせるので，型が無いまま翻訳させて落ちていた。
 *
 * 上流の configure が --config-backtrace=no / --config-bcheck=no で
 * 書くのと同じ 2 行である (configure 704〜705 行)。対になる
 * CONFIG_backtrace=no / CONFIG_bcheck=no は config.mak 側に置く
 * (tools/tcc17.sh)。**両方揃っていないと意味がない** —— 片方だけだと
 * Makefile と ソースの言い分が食い違ったままになる */
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
