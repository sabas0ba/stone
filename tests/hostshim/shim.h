/* ホストの処理系でプローブを走らせるための繋ぎ (tools/diff17.sh)。
 *
 * プローブは我々の前置部が提供する putc(int) を直に呼ぶ。ホストでは
 * それが stdio の putc(int, FILE *) とぶつかるので，名前を差し替える。
 *
 * **プローブ本体は 1 文字も変えない。** 変えたら「同じソースを両方で
 * 走らせた」と言えなくなる (docs/stage017-gcc.md 5.2)。 */
#include <stdio.h>
#include <stdarg.h>
#define putc  stone_putc
static int stone_putc(int c) { fputc(c, stdout); return c; }
