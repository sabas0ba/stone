# 上の階層から $(MAKE) -C で呼ばれる側 (第 3 部の 3 の 1)

CPS = a.cp b.cp

all: libx

libx: $(CPS)
	cat $^ > $@

%.cp: %.src
	cat $< > $@

.PHONY: all
