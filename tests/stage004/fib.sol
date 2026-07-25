# 再帰関数・算術・バッファの実行テスト。fib(10) = 55 を 10 進で出力する。
var i
buf digits 16

fn prn              # ( n -- ) n >= 0 を 10 進で出力
  0 i !
  begin
    dup 10 % '0' +
    digits i @ + c!
    i @ 1 + i !
    10 /
    dup not
  until
  drop
  begin i @ while
    i @ 1 - i !
    digits i @ + c@ putc
  repeat
end

fn fib              # ( n -- fib(n) )
  dup 2 < if ret then
  dup 1 - fib
  swap 2 - fib
  +
end

fn main
  10 fib prn
  10 putc
  0 exit
end
.
