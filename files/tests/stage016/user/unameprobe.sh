# uname は我々の OS の素性を答えるものなので，本物の uname とは必ず
# 食い違う。参照シェルで期待値を作れないのはここだけである。
#
# **並び順は本物に合わせてある。** uname は選択肢を書いた順ではなく
# -s -n -r -v -m -p -i -o の順に並べる。configure は `uname -m -s` と
# 書くので，ここを取り違えると値が入れ替わる。本物の uname が
# `uname -m -s` に "Linux x86_64" (sysname が先) を返すことを見て決めた。
uname
uname -s
uname -m
uname -r
uname -o
uname -p
uname -m -s
