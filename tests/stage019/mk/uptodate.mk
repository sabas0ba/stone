# 古さの判定: 2 度目は何も作らない (tests/stage019/test.sh)
all: t2
t1: in
	@echo making t1
	@echo t1 > t1
t2: t1
	@echo making t2
	@echo t2 > t2
