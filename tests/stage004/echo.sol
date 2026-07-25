# フィルタ動作の実行テスト。'.' が現れるまで入力をそのまま出力する。
fn main
  begin getc dup '.' <> while
    putc
  repeat
  drop
  0 exit
end
.
