# 文字列リテラルとループの実行テスト。"hello" + LF を出力して正常終了する。
fn main
  " hello"
  begin dup c@ dup while
    putc
    1 +
  repeat
  drop drop
  10 putc
  0 exit
end
.
