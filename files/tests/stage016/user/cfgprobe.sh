# tcc の configure (768 行) を我々の OS の上で走らせる。
# 第 4 部の完了条件そのものである (docs/stage016-os.md 11.6)。
#
# --cc=false を渡すのは，我々の OS にコンパイラが**コマンドとして**
# 無いからである。false は本物のシェルにも我々のシェルにも組込みで
# あり，どちらでも同じに「起動できて失敗する」。ここを gcc のまま
# にすると，参照実行の側だけ本物の gcc を見つけてしまい，比べる
# 意味が無くなる。
#
# 1 回目は --cpu を渡さない。configure は uname -m を見て機種を
# 決めるので，我々の OS では riscv32 が返り，tcc の知らない機種
# として弾かれる。**これが我々の機械での正しい答えである。**
sh2 configure --cc=false
echo "==== rc-nocpu $?"
# 2 回目は機種を明示して最後まで通す。config.h / config.mak /
# config.texi が出る
sh2 configure --cc=false --cpu=x86_64
echo "==== rc-cpu $?"
echo "==== config.h"
cat config.h
echo "==== config.mak"
cat config.mak
echo "==== config.texi"
cat config.texi
echo done
