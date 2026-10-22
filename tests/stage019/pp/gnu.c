#define e1(fmt, ...) f(fmt, ## __VA_ARGS__)
#define e2(fmt, args...) g(fmt , ## args)
#define e3(x...) h(x)
e1(a); e1(a, b, c); e2(a); e2(a, b); e3(); e3(1, 2);
#define str(s) #s
#define diag(s) _Pragma(str(GCC diagnostic s))
diag(push) int p;
int c0 = __COUNTER__, c1 = __COUNTER__;
#if -1 > 0u
unsigned_ok;
#else
unsigned_ng;
#endif
#if (2 + 3 * 4) == 14 && !defined(UNDEF) && defined NEST
#elif 1 << 3 == 8
elif_ok;
#endif
#define NEST
#if defined(NEST) ? 0x10 % 3 == 1 : 0
cond_ok;
#endif
#include "guard.h"
#include "guard.h"
#include <next.h>
#define EMPTY
#define cat(a, b) a ## b
#define xcat(a, b) cat(a, b)
x EMPTY y; xcat(ab, 12); cat(, z); -EMPTY-1;
#define va(x, ...) v(x, ## __VA_ARGS__)
va(1) va(1,) va(1, 2);
#define ln(a) a __LINE__
ln(__LINE__
) ln(
__LINE__);
#line 100 "logical.c"
__LINE__ __FILE__;
#if 0 && (1 / 0)
sc_bad;
#elif 1 || (1 / 0)
sc_ok1;
#endif
#if 0 ? 1 / 0 : 2
sc_ok2;
#endif
