# tccish.mk と同じ 6 つの形を，我々の OS の上で走らせられる命令に
# 置き換えたもの (第 3 部の 3 の 1)。翻訳器は使わない —— 見たいのは
# **mk がどの命令をどの順で出すか**であって，翻訳ではないからである。
#
# 変数の値を見えるようにするため，DEFS には実在するファイルを入れて
# cat の引数にしてある。目標特有の += が凍っていれば ($(EXTRA-DEFS)
# のまま渡れば) cat が開けずに落ちる

DEFS = base.txt
EXTRA-DEFS = extra.txt

SRC = $(wildcard src/*.src)
CPS = $(patsubst %.src,%.cp,$(SRC))

all: prog cross-lib lib
	@echo "done $(firstword $(CPS))"

# src/main.cp だけ依存を 1 つ足す。$< は config.h ではなく src/main.src
src/main.cp: config.h
src/main.cp: DEFS += $(EXTRA-DEFS)

%.cp: %.src
	cat $(DEFS) $< > $@

%.cp: %.asm
	cat $< > $@

prog: $(CPS) asm.cp
	cat $^ > $@

# 中身の無い命令。型規則より優先され，「何もしない」が実現する
cross-lib : ;

lib:
	$(MAKE) -C lib

.PHONY: all lib cross-lib
