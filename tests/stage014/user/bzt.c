/* bzt.c --- libbz2 (外部ソース) の実走検査
 *
 * 既知のデータを圧縮し，伸長して往復一致を確かめる。
 * bzip2 1.0.8 の低水準 API (BZ_NO_STDIO 版) を使う。
 */
#define BZ_NO_STDIO 1
#include <stdio.h>
#include <stdlib.h>
#include "bzlib.h"

#define SRCN 4096

unsigned char src[SRCN];
unsigned char comp[8192];
unsigned char back[8192];

/* BZ_NO_STDIO 版の libbz2 は検査失敗時にこれを呼ぶ (bzlib.h 参照) */
void bz_internal_error(int errcode) {
    printf("bz_internal_error %d\n", errcode);
    exit(3);
}

int main() {
    unsigned int clen;
    unsigned int dlen;
    int r;
    int i;
    unsigned int seed;
    int sum;
    char *msg;

    /* 前半は繰り返しテキスト (圧縮が効く)，後半は擬似乱数 (効かない) */
    msg = "stone bootstrap chain meets bzip2! ";
    for (i = 0; i < 2048; i++) src[i] = msg[i % 35];
    seed = 12345;
    for (i = 2048; i < SRCN; i++) {
        seed = seed * 1103515245 + 12345;
        src[i] = (seed >> 16) & 0xFF;
    }

    clen = 8192;
    r = BZ2_bzBuffToBuffCompress((char *)comp, &clen, (char *)src, SRCN, 1, 0, 0);
    if (r != 0) { printf("compress err %d\n", r); return 1; }
    sum = 0;
    for (i = 0; i < (int)clen; i++) sum = (sum + comp[i]) & 0xFFFF;
    printf("compress: %d -> %d sum=%d\n", SRCN, (int)clen, sum);

    dlen = 8192;
    r = BZ2_bzBuffToBuffDecompress((char *)back, &dlen, (char *)comp, clen, 0, 0);
    if (r != 0) { printf("decompress err %d\n", r); return 1; }
    if ((int)dlen != SRCN) { printf("length mismatch %d\n", (int)dlen); return 1; }
    for (i = 0; i < SRCN; i++)
        if (back[i] != src[i]) { printf("byte mismatch at %d\n", i); return 1; }
    printf("roundtrip ok\n");

    /* 圧縮結果と元データをファイルへ残す。ホスト側の bzip2 -d で
     * 伸長できること (形式の相互運用) の検査に使う */
    {
        FILE *f;
        f = fopen("out.bz2", "w");
        if (f == 0) { printf("fopen out\n"); return 1; }
        fwrite(comp, 1, clen, f);
        fclose(f);
        f = fopen("src.bin", "w");
        if (f == 0) { printf("fopen src\n"); return 1; }
        fwrite(src, 1, SRCN, f);
        fclose(f);
    }
    return 0;
}
