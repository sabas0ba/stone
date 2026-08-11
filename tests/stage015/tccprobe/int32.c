/* 第 5 部 その 2 の検査対象。
 * riscv32-tcc にこれを吐かせ，RV32 に無い命令が 1 つも無いことを見る
 * (docs/stage015-riscv32.md 4.3 の 1・2)。
 *
 * 整数まわりを一通り触る。呼出し規約 (その 3) と浮動小数点 (その 4) は
 * まだ入っていないので，ここでは int と語の範囲に留める。 */

int g;
char cb;
short sh;
unsigned char ub;
unsigned short us;

/* 乗除算 (M 拡張)。RV64 では mulw / divw / remw になるところ */
int arith(int a, int b) { return a * b - a / b + a % b; }
unsigned uarith(unsigned a, unsigned b) { return a * b / b % (b + 1); }

/* シフト。RV64 では sllw / srlw / sraw，桁数の切り出しも 63 でなく 31 */
int shifts(int a, int n) { return (a << n) + (a >> n) + ((unsigned)a >> n); }
int shiftc(int a) { return (a << 3) + (a >> 5) + ((unsigned)a >> 7); }

/* 比較 */
int cmps(int a, int b)
{
    return (a < b) + (a >= b) + ((unsigned)a < (unsigned)b) + (a == b) + (a != b);
}

/* 幅の狭い型との行き来 (slli/srai の桁数が語幅で決まる) */
int conv(void) { return cb + sh + ub + us; }
void conv2(int x) { cb = x; sh = x; ub = x; us = x; }

/* ポインタ演算 (語幅で刻みが変わる) */
int *padd(int *p, int i) { return p + i; }
int pdiff(int *p, int *q) { return p - q; }
int deref(int *p) { return *p + p[3]; }

/* 大域と局所，枠の出し入れ */
int locals(int n)
{
    int a[8];
    int i, s = 0;
    for (i = 0; i < 8; i++)
        a[i] = i * n;
    for (i = 0; i < 8; i++)
        s += a[i];
    g = s;
    return s;
}

/* 分岐 */
int sw(int x)
{
    switch (x) {
    case 1: return 2;
    case 7: return 9;
    case 100: return 101;
    default: return 0;
    }
}

int loop(int n)
{
    int i, s = 0;
    for (i = 0; i < n; i++)
        s += i;
    while (s > 1000)
        s -= 1000;
    do { s++; } while (s < 0);
    return s;
}

/* 関数呼出し (int だけ。語をまたぐ引数は その 3) */
int callit(int x) { return arith(x, 3) + shifts(x, 2) + sw(x) + locals(x); }

/* 大きな定数 (lui + addi。RV64 では addiw になるところ) */
int bigconst(int x) { return x + 0x12345678 + 0x7ffff000; }
