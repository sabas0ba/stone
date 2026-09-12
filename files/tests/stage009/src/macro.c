#define ADD(a, b) ((a) + (b))
#define TWICE(x) ADD(x, x)
#define REC REC + 1
#define SELF(x) SELF(x)
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a ## b
#define VER 42
#define EMPTY
#define LOG(fmt, ...) pr(fmt, __VA_ARGS__)
#define NOARG(...) g(__VA_ARGS__)
r1 = TWICE(3);
r2 = REC;
r3 = SELF(9);
s1 = STR(VER);
s2 = XSTR(VER);
s3 = STR(a "q" b);
c1 = CAT(foo, bar);
c2 = CAT(x, VER);
e1 = EMPTY;
v1 = LOG("a", 1, 2);
v2 = NOARG();
v3 = NOARG(7);
n1 = ADD(1,
         2);
t1 = ADD /* comment */ (5, 6);
t2 = "ADD(1,2) is a string";
#undef VER
u1 = VER;
