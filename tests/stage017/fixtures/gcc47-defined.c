#define GCC_VERSION (__GNUC__ * 1000 + __GNUC_MINOR__)
#define HAVE_DESIGNATED_INITIALIZERS \
  (!defined(__cplusplus) \
   && ((GCC_VERSION >= 2007) || (__STDC_VERSION__ >= 199901L)))
#if HAVE_DESIGNATED_INITIALIZERS
a_yes
#else
a_no
#endif
