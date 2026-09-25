/* 期待値の正解を riscv32-tcc に確かめる静的表明 (tests/stage015 が使う)。
   通れば「その値が正しい」。落ちれば期待のほうが誤り。 */
/* riscv32-tcc を正解として，配置の規則を静的表明で確かめる。
   通れば「その値が正しい」。落ちれば期待のほうが誤り。 */
#define AS(n, e) int n[(e) ? 1 : -1]

struct SA { unsigned short a:5,b:1,c:1,d:2,e:1,f:1,g:1,h:1,i:1,x:2; };
struct SC { unsigned char  a:3,b:2,c:3; };
struct SI { unsigned       a:5,b:1,c:1,d:2,e:1,f:1,g:1,h:1,i:1,x:2; };
struct SC2 { unsigned char a:3,b:2,c:3,d:1; };        /* 8 bit を越える */
struct M1 { char c; };
struct M2 { char c; short s; };
struct M3 { int v; long long ll; };
struct M4 { char c; long long ll; };
struct M5 { char c; double d; };
struct M6 { long long ll; char c; };                  /* 末尾の詰め */
struct M7 { char a; struct M2 m; };                   /* 入れ子の整列 */
struct M8 { char a; char b[3]; };

AS(t01, sizeof(struct SA) == 2);
AS(t02, sizeof(struct SC) == 1);
AS(t03, sizeof(struct SI) == 4);
AS(t04, sizeof(struct SC2) == 2);
AS(t05, sizeof(struct M1) == 1);
AS(t06, sizeof(struct M2) == 4);
AS(t07, sizeof(struct M3) == 16);
AS(t08, (int)&((struct M3 *)0)->ll == 8);
AS(t09, sizeof(struct M4) == 16);
AS(t10, (int)&((struct M4 *)0)->ll == 8);
AS(t11, sizeof(struct M5) == 16);
AS(t12, sizeof(struct M6) == 16);
AS(t13, sizeof(struct M7) == 6);
AS(t14, (int)&((struct M7 *)0)->m == 2);
AS(t15, sizeof(struct M8) == 4);
