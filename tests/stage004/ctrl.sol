# 変数・while ループ・if/else の実行テスト。"ABCDE" 改行 "YN" 改行を出力する。
var n
const newline 10

fn main
  0 n !
  begin n @ 5 < while
    n @ 65 + putc
    n @ 1 + n !
  repeat
  newline putc
  3 7 < if 89 putc else 78 putc then    # 'Y'
  1 2 > if 89 putc else 78 putc then    # 'N'
  newline putc
  0 exit
end
.
