/* 素の実行環境の小さな libc (docs/stage018-ncc.md 7 章の 2)。ncc1 が訳す。
 *
 * 前処理器の指令を使わない (ncc1 は前処理済みの入力を読む)。可変長の
 * 引数は __builtin_va_* で読む。printf はホストの printf と同じ出力を
 * 出すことを試験が前提にするので，持つ変換はその範囲に絞る:
 * %d %i %u %x %X %o %c %s %p %% と，長さの l / ll，旗 - 0，幅。
 */

typedef __builtin_va_list va_list;
typedef unsigned int size_t;

void exit(int code);

int putchar(int c)
{
    volatile unsigned char *uart = (volatile unsigned char *)0x10000000;
    while ((uart[5] & 0x20) == 0)
        ;
    uart[0] = (unsigned char)c;
    return c & 0xff;
}

int puts(const char *s)
{
    while (*s)
        putchar(*s++);
    putchar('\n');
    return 0;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = d;
    while (n--)
        *dp++ = (unsigned char)c;
    return d;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* 64 bit の除算は実行時支援を呼ぶ (ncc1 は除算・剰余だけを展開しない) */
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d,
                                unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1ULL << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long n, unsigned long long d)
{
    return __udivmoddi4(n, d, 0);
}

unsigned long long __umoddi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    __udivmoddi4(n, d, &r);
    return r;
}

long long __divdi3(long long n, long long d)
{
    int neg = (n < 0) != (d < 0);
    unsigned long long q = __udivmoddi4(n < 0 ? -(unsigned long long)n : n,
                                        d < 0 ? -(unsigned long long)d : d, 0);
    return neg ? -(long long)q : (long long)q;
}

long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    __udivmoddi4(n < 0 ? -(unsigned long long)n : n, d < 0 ? -(unsigned long long)d : d, &r);
    return n < 0 ? -(long long)r : (long long)r;
}

static int out_num(unsigned long long v, int base, int upper, int neg, int width, int zero,
                   int left)
{
    char buf[24];
    int n = 0, len, i, count = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        buf[n++] = digits[v % base];
        v /= base;
    } while (v);
    len = n + neg;
    if (!left && !zero)
        for (i = len; i < width; i++, count++)
            putchar(' ');
    if (neg) {
        putchar('-');
        count++;
    }
    if (!left && zero)
        for (i = len; i < width; i++, count++)
            putchar('0');
    while (n > 0) {
        putchar(buf[--n]);
        count++;
    }
    if (left)
        for (i = len; i < width; i++, count++)
            putchar(' ');
    return count;
}

int vprintf(const char *fmt, va_list ap)
{
    int count = 0;
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, width = 0, lng = 0;
        if (*fmt != '%') {
            putchar(*fmt);
            count++;
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = lng >= 2 ? __builtin_va_arg(ap, long long)
                : lng == 1 ? __builtin_va_arg(ap, long) : __builtin_va_arg(ap, int);
            unsigned long long u = v < 0 ? -(unsigned long long)v : (unsigned long long)v;
            count += out_num(u, 10, 0, v < 0, width, zero, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v = lng >= 2 ? __builtin_va_arg(ap, unsigned long long)
                : lng == 1 ? __builtin_va_arg(ap, unsigned long) : __builtin_va_arg(ap, unsigned int);
            int base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
            count += out_num(v, base, *fmt == 'X', 0, width, zero, left);
            break;
        }
        case 'p': {
            unsigned int v = (unsigned int)__builtin_va_arg(ap, void *);
            putchar('0');
            putchar('x');
            count += 2 + out_num(v, 16, 0, 0, 0, 0, 0);
            break;
        }
        case 'c':
            putchar(__builtin_va_arg(ap, int));
            count++;
            break;
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            int len = (int)strlen(s), i;
            if (!left)
                for (i = len; i < width; i++, count++)
                    putchar(' ');
            while (*s) {
                putchar(*s++);
                count++;
            }
            if (left)
                for (i = len; i < width; i++, count++)
                    putchar(' ');
            break;
        }
        case '%':
            putchar('%');
            count++;
            break;
        default:
            putchar('?');
            count++;
            break;
        }
    }
    return count;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    __builtin_va_start(ap, fmt);
    n = vprintf(fmt, ap);
    __builtin_va_end(ap);
    return n;
}
