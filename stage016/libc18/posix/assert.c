/* assert.c --- assert の失敗時の報告 (第 14 世代で追加)
 *
 * 式の文字列を標準エラーへ出して exit(1) する。abort (シグナル) は
 * 無いので終了コードで表す。
 */
#include <stdio.h>
#include <assert.h>

/* 前置部が提供する */
int exit(int code);

int __assert(char *s) {
  fputs("assertion failed: ", stderr);
  fputs(s, stderr);
  fputs("\n", stderr);
  exit(1);
  return 0;
}
