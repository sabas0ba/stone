# tcc の Makefile と同じ**形**だけを取り出した記述 (第 3 部の 3 の 1)。
#
# 完了条件そのものは本物の tcc の Makefile で見る (docs/stage017-cc.md
# 15 章) が，素材は repo に入れない決まりなので CI では飛ぶ。ここは
# **同じ 6 つの形を自前で並べて，CI でも本物の make と突き合わせる**
# ための記述である。
#
#   1. $(wildcard) の * で元を集める
#   2. %.o: %.c と %.o: %.S が並ぶ (依存が在るほうを選べるか)
#   3. 目標特有の変数の += (遅延の変数を凍らせないか)
#   4. 型規則で作る目標に，別の行で依存を足す ($< がどちらを指すか)
#   5. t: dep ; cmd の形 (中身が空でも「命令を持つ規則」になる)
#   6. $(MAKE) -C で下の階層へ降りる

CC = cc18
DEFS = -DBASE
EXTRA-DEFS = -DEXTRA

# 1. src/ から集める。並びは make が sort するので順序も揃う
SRC = $(wildcard src/*.c)
OBJS = $(patsubst %.c,%.o,$(SRC))

all: prog cross-lib
	@echo "done $(firstword $(OBJS))"

# 4. src/main.o は型規則で作るが，依存はもう 1 行足す。
#    $< は config.h ではなく src/main.c でなければならない
src/main.o: config.h

# 3. この目標だけ DEFS を伸ばす。EXTRA-DEFS は遅延のままで良い
src/main.o: DEFS += $(EXTRA-DEFS)

# 2. 同じ %.o に 2 つの型規則。asm.o は .S しか無い
%.o: %.c
	$(CC) $(DEFS) -c $< -o $@

%.o: %.S
	$(CC) -c $< -o $@

prog: $(OBJS) asm.o
	$(CC) -o $@ $^

# 5. 中身の無い命令。型規則より優先され，「何もしない」が実現する
cross-lib : ;

# 6. 下の階層。-n は $(MAKE) の値に入っているので下も見るだけになる
lib:
	$(MAKE) -C lib

.PHONY: all lib cross-lib
