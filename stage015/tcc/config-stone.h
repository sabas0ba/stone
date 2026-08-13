/* config-stone.h --- stone 上で tcc を組むときの config.h
 *
 * 上流の configure はホストでしか動かないので，stone 用の設定を
 * 手で固定する。束ねでは "config.h" の名前で載せる
 * (tools/bundle.sh の 名前=パス の形)。
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
