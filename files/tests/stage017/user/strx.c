/* 別の翻訳単位その 2。libc の関数も使う (書庫と /lib の両方が要る) */
#include <string.h>
#include "strx.h"

int lenx(char *s) { return (int)strlen(s); }

void catx(char *d, char *s) { strcat(d, s); }
