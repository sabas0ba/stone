# 型規則・静的型規則・順序だけの依存・目標特有の変数・VPATH・複数の目標の型規則・
# 二次展開・FORCE (tests/stage019/test.sh)
VPATH := src
CFLAGS := -O
out/b.o: CFLAGS += -DB
%.pv: PV = pattern-var
all: out/a.o out/b.o out/s1.x out/s2.x out/g.tab.c x.pv inh sec | out
	@echo all: $^ order-only: $|
out/%.o: %.c FORCE
	@echo cc $(CFLAGS) $< to $@ stem $*
	@echo obj > $@
out/s1.x out/s2.x: out/%.x: a.c
	@echo static $@ from $< stem $*
	@echo x > $@
out/%.tab.c out/%.tab.h: %.y
	@echo yacc $< to $@ grouped
	@echo c > out/$*.tab.c
	@echo h > out/$*.tab.h
x.pv:
	@echo pv $(PV)
inh: TV = inherited
inh: child
child:
	@echo child sees $(TV)
out:
	@echo order-only out exists
.SECONDEXPANSION:
sec: $$(LATE)
	@echo sec deps $^
LATE = late
late:
	@echo late
FORCE:
.PHONY: all inh child x.pv sec late FORCE
