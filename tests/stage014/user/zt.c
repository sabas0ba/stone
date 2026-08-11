/* zt.c --- zlib (外部ソース 2 本目) の実走検査
 *
 * 既知のデータの crc32 / adler32 を計り，deflate -> inflate の往復一致を
 * 確かめる。zlib 1.3.1 の Z_SOLO 構成 (割付けは利用側が渡す) を使う。
 */
#define Z_SOLO 1
#include <stdio.h>
#include <stdlib.h>
#include "zlib.h"

#define SRCN 4096

unsigned char src[SRCN];
unsigned char comp[8192];
unsigned char back[8192];

static voidpf zall(voidpf o, uInt items, uInt size) {
    return malloc(items * size);
}
static void zfr(voidpf o, voidpf p) {
    free(p);
}

int main() {
    z_stream ds;
    z_stream is;
    unsigned long c;
    unsigned long a;
    int r;
    int i;
    unsigned int seed;
    int sum;
    unsigned int clen;
    char *msg;

    msg = "stone bootstrap chain meets zlib! ";
    for (i = 0; i < 2048; i++) src[i] = msg[i % 34];
    seed = 12345;
    for (i = 2048; i < SRCN; i++) {
        seed = seed * 1103515245 + 12345;
        src[i] = (seed >> 16) & 0xFF;
    }

    /* 既知データのチェックサム (ホスト側の zlib と突き合わせる) */
    c = crc32(0, (Bytef *)0, 0);
    c = crc32(c, src, SRCN);
    a = adler32(1, src, SRCN);
    printf("crc32=%x adler32=%x\n", (unsigned)c, (unsigned)a);

    /* deflate (1 発呼び。Z_FINISH で全部書き切る) */
    ds.zalloc = zall;
    ds.zfree = zfr;
    ds.opaque = 0;
    r = deflateInit_(&ds, 6, "1.3.1", (int)sizeof(z_stream));
    if (r != 0) { printf("deflateInit err %d\n", r); return 1; }
    ds.next_in = src;
    ds.avail_in = SRCN;
    ds.next_out = comp;
    ds.avail_out = 8192;
    r = deflate(&ds, 4);
    if (r != 1) { printf("deflate err %d\n", r); return 1; }
    clen = 8192 - ds.avail_out;
    deflateEnd(&ds);
    sum = 0;
    for (i = 0; i < (int)clen; i++) sum = (sum + comp[i]) & 0xFFFF;
    printf("deflate: %d -> %d sum=%d\n", SRCN, (int)clen, sum);

    /* inflate して往復一致を見る */
    is.zalloc = zall;
    is.zfree = zfr;
    is.opaque = 0;
    is.next_in = comp;
    is.avail_in = clen;
    r = inflateInit_(&is, "1.3.1", (int)sizeof(z_stream));
    if (r != 0) { printf("inflateInit err %d\n", r); return 1; }
    is.next_out = back;
    is.avail_out = 8192;
    r = inflate(&is, 4);
    if (r != 1) { printf("inflate err %d\n", r); return 1; }
    if ((int)(8192 - is.avail_out) != SRCN) { printf("length mismatch\n"); return 1; }
    inflateEnd(&is);
    for (i = 0; i < SRCN; i++)
        if (back[i] != src[i]) { printf("byte mismatch at %d\n", i); return 1; }
    printf("roundtrip ok\n");

    /* 圧縮結果と元データを残す。ホスト側 zlib での伸長 (相互運用) に使う */
    {
        FILE *f;
        f = fopen("out.zz", "w");
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
