#define N 3
#if N * 2 > 5 && defined(N)
a_yes
#else
a_no
#endif
#if 0
  #if 1
  never
  #endif
  b_no
#elif N == 3
b_yes
#else
b_other
#endif
#ifndef Q
c_yes
#endif
#undef N
#ifdef N
d_no
#else
d_yes
#endif
#if (1 ? 2 : 3) == 2 && (0xff & 0x0f) == 15 && (1 << 4) == 16 && !0 && ~0 == -1
e_yes
#endif
#if UNDEF_THING
f_no
#else
f_yes
#endif
#if 0
#define SHOULD_NOT_DEFINE 1
#include "no-such-header.h"
#error not reached
#endif
#ifdef SHOULD_NOT_DEFINE
g_no
#else
g_yes
#endif
