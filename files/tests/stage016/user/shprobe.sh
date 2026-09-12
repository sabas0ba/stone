# sh2 が我々の OS の上でホストと同じ意味論で動くかを見る。
#
# 出力は 1 行 1 件の「名札 期待 実測」で，突き合わせは expected/ が持つ。
# ここに並べたのは tcc の configure が実際に使っている書き方である
# (docs/stage016-os.md 9.2 / 9.6)。
V=hello
echo "var $V hello"
echo 'quote $V $V'
echo "cmdsub sub $(echo sub)"
A=x.y.z
echo "trim-h y.z ${A#*.}"
echo "trim-hh z ${A##*.}"
echo "trim-p x.y ${A%.*}"
echo "trim-pp x ${A%%.*}"
echo "default fallback ${NOPE:-fallback}"
echo "assign set ${W:=set}"
echo "assigned set $W"
test -z "" && echo "test-z ok ok"
[ a = a ] && echo "test-eq ok ok"
[ 1 -lt 2 ] && echo "test-lt ok ok"
for w in a b; do echo "for-$w $w $w"; done
case abc in
  x*) echo "case bad bad" ;;
  a*) echo "case ok ok" ;;
esac
f() { echo "fn 2 $#"; }
f one two
g() { local L=in; G=glob; echo "local in $L"; }
L=out
g
echo "local-after out $L"
echo "global-after glob $G"
good_split() { set -- "$1" $1; test $# = 2 && test "$1" = "$2"; }
good_split abc && echo "split ok ok"
good_split "a b" || echo "split2 ok ok"
assign_opt() { set -- "${2:-${1%%=*}}" "${1#*=}"; eval ${1#--}=\$2; }
assign_opt --prefix=/usr/local
echo "eval-assign /usr/local $prefix"
CC=
test -n "$CC" || echo "quoted-empty ok ok"
set -- p q r
echo "setpos 3 $#"
echo "pos2 q $2"
shift
echo "shift q $1"
# 外部コマンドを使わずに回す (OS 側には expr が無い)
set -- a b c
n=
while [ $# -gt 0 ]; do shift; n=1$n; done
echo "while 111 $n"
# **中身を 1 つも走らせなかった複合コマンドの状態は 0 である。**
# configure の最後の行がこの形で，ここを取り違えると configure 全体が
# 1 で終わる。走ったかどうかで答が変わるので，両側を見る
false
if test a = b; then echo no; fi
echo "if-noelse 0 $?"
false
while false; do echo no; done
echo "while-none 0 $?"
false
until true; do echo no; done
echo "until-none 0 $?"
false
for x in $NOTHING; do echo no; done
echo "for-none 0 $?"
false
case zz in a) echo no ;; esac
echo "case-nohit 0 $?"
false
h() { echo hi; }
echo "func-def 0 $?"
echo done
