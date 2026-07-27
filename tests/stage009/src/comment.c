a /* block
   spanning lines */ b // line comment
c = "not /* a */ comment";
d = 1 /**/ 2;
e = '/'; // don't be fooled
#define J(x) x /* comment in body */
f = J(9);
g = 1 + \
    2;
