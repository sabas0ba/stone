# 変数・関数・条件・define/eval (tests/stage019/test.sh)
A = a $(B)
B = b
C := $(A) c
D += d1
D += d2
E ?= e
E ?= no
F = x.o y.o z.c
f = [$(1),$(2)]
define two
@echo first
@echo second
endef
ifeq ($(B),b)
O = eq
else ifeq ($(B),c)
O = c
else
O = ne
endif
ifdef B
P = def
endif
ifndef UNDEF
Q = undef
endif
$(eval R = from-eval)
$(foreach v,S1 S2,$(eval $(v) := set-$(v)))
all:
	@echo A=$(A) C=$(C) D=$(D) E=$(E)
	@echo G=$(F:.o=.c) $(patsubst %.c,%.h,$(F)) $(patsubst %, -G %,u v)
	@echo H=$(foreach w,1 2 3,[$(w)]) I=$(call f,p,q) $(call f,r)
	@echo J=$(if $(B),yes,no) $(if ,yes,no) $(or ,,z) $(and a,b)
	@echo K=$(sort c b a b) $(words a b c) $(firstword a b) $(lastword a b) $(word 2,a b c) $(wordlist 2,3,a b c d)
	@echo L=$(dir a/b.c c) $(notdir a/b.c c) $(basename a/b.c d.e/f) $(suffix a/b.c d) $(addprefix p-,a b) $(addsuffix .x,a b)
	@echo M=$(subst a,A,banana) $(strip   a   b  ) $(findstring an,banana) $(filter %.c %.h,a.c b.o c.h) $(filter-out %.c,a.c b.o)
	@echo N=$(origin A) $(origin UNDEF) $(origin all) $(flavor A) $(flavor C)
	@echo O=$(O) P=$(P) Q=$(Q) R=$(R) S=$(S1) $(S2)
	$(two)
.PHONY: all
